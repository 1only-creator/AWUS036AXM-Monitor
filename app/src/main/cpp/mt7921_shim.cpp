/**
 * mt7921_shim.cpp
 * ===============
 * Userspace reimplementation of the mt76 / mt7921 Linux kernel driver
 * using libusb for USB access.
 *
 * Key design decisions
 * --------------------
 *  • We DON'T link against the original kernel .ko.
 *    Instead we copy the *logic* (register offsets, MCU command formats,
 *    firmware upload sequence) from the mt76 source tree and replace all
 *    kernel-API calls with libusb / POSIX / Android-NDK equivalents.
 *
 *  • Firmware blobs (WIFI_RAM_CODE_MT7961_1.bin, patch .bin) must be
 *    present in the app's files/ directory.  The Kotlin layer is
 *    responsible for copying them from APK assets on first launch.
 *
 *  • All captured 802.11 frames are forwarded as a pcap-over-TCP stream
 *    on 127.0.0.1:37008.  The global pcap file header is sent once upon
 *    client connection; thereafter each frame is wrapped in a pcap record
 *    header.  This lets Wireshark / tcpdump consume the stream live.
 *
 * Sources / references
 * --------------------
 *  https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76
 *  https://github.com/emanuele-f/UsbWifiMonitorApi
 */

#include "mt7921_shim.h"
#include "compat.h"

#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <android/log.h>

/* ── Internal helpers ─────────────────────────────────────────────────── */

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "MT7921", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "MT7921", __VA_ARGS__)
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, "MT7921", __VA_ARGS__)

/* MT7921AU USB endpoints (from Linux driver source) */
#define EP_BULK_OUT  0x02   /* host → device commands + TX */
#define EP_BULK_IN   0x84   /* device → host RX data       */
#define EP_CMD_IN    0x88   /* device → host MCU responses */

/* MCU command timeout ms */
#define MCU_TIMEOUT_MS 3000
/* Maximum bulk RX frame size (WiFi 6 MPDU with A-MSDU) */
#define RX_BUF_SIZE    (4096 + 512)

/* ── Radiotap minimal header (8 bytes) ───────────────────────────────── */
#pragma pack(push,1)
typedef struct {
    u8  it_version;   /* 0 */
    u8  it_pad;
    u16 it_len;       /* 8 – total header length */
    u32 it_present;   /* 0 – no optional fields  */
} radiotap_hdr_t;

/* pcap global header */
typedef struct {
    u32 magic;        /* 0xa1b2c3d4 */
    u16 version_major;
    u16 version_minor;
    s32 thiszone;
    u32 sigfigs;
    u32 snaplen;
    u32 network;      /* 127 = LINKTYPE_IEEE802_11_RADIOTAP */
} pcap_global_hdr_t;

/* pcap record header */
typedef struct {
    u32 ts_sec;
    u32 ts_usec;
    u32 incl_len;
    u32 orig_len;
} pcap_rec_hdr_t;
#pragma pack(pop)

/* ── MCU command buffer ──────────────────────────────────────────────── */
#pragma pack(push,1)
typedef struct {
    u32 info_type;   /* bit 31 = query/set, bits [15:8] = seq */
    u32 length;
    u8  data[0];
} mcu_cmd_hdr_t;
#pragma pack(pop)

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 1 – USB helpers                                                 */
/* ─────────────────────────────────────────────────────────────────────── */

static int usb_bulk_out(mt7921_dev_t *d, const u8 *buf, int len) {
    int transferred = 0;
    int r = libusb_bulk_transfer(d->dev, EP_BULK_OUT,
                                 (unsigned char *)buf, len,
                                 &transferred, MCU_TIMEOUT_MS);
    if (r != LIBUSB_SUCCESS) {
        LOGE("bulk_out error: %s (sent %d/%d)", libusb_strerror((libusb_error)r), transferred, len);
        return -EIO;
    }
    return transferred;
}

static int usb_bulk_in(mt7921_dev_t *d, u8 *buf, int max_len, int *actual) {
    int r = libusb_bulk_transfer(d->dev, EP_BULK_IN,
                                 buf, max_len, actual, MCU_TIMEOUT_MS);
    if (r == LIBUSB_ERROR_TIMEOUT) return -ETIMEDOUT;
    if (r != LIBUSB_SUCCESS) {
        LOGE("bulk_in error: %s", libusb_strerror((libusb_error)r));
        return -EIO;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 2 – MCU commands                                                */
/* ─────────────────────────────────────────────────────────────────────── */

/**
 * mt7921_mcu_send_cmd
 * Wraps an arbitrary payload in the mt76 MCU command envelope and sends
 * it via bulk-OUT endpoint.
 *
 * @cmd_id: MCU_EXT_CMD_* constant
 * @payload: command-specific bytes
 * @len: payload length
 */
static int mt7921_mcu_send_cmd(mt7921_dev_t *d, u8 cmd_id,
                                const u8 *payload, u32 len)
{
    /* The mt76 MCU command envelope is 12 bytes:
     *   [0..3]  = (type << 24) | (seq << 16) | cmd_id
     *   [4..7]  = total length (header + payload)
     *   [8..11] = extended command ID for EXT commands
     * We keep it simple: type=0x01 (set), seq increments per call.
     */
    static u8 seq = 0;
    u32 total = 12 + len;
    u8 *buf = (u8 *)malloc(total);
    if (!buf) return -ENOMEM;

    u32 header = cpu_to_le32((0x01u << 24) | ((u32)(seq++) << 16) | cmd_id);
    u32 length = cpu_to_le32(total);

    memcpy(buf + 0, &header, 4);
    memcpy(buf + 4, &length, 4);
    memset(buf + 8, 0, 4);          /* reserved / ext cmd filler */
    if (len) memcpy(buf + 12, payload, len);

    int r = usb_bulk_out(d, buf, (int)total);
    free(buf);
    return (r < 0) ? r : 0;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 3 – Firmware upload                                             */
/* ─────────────────────────────────────────────────────────────────────── */

/**
 * Loads one firmware blob to the chip via scatter DMA.
 * Mirrors mt76_mcu_send_firmware() in the kernel driver.
 */
static int upload_blob(mt7921_dev_t *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGE("Cannot open firmware: %s  (errno %d)", path, errno);
        return -ENOENT;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    u8 *blob = (u8 *)malloc(sz);
    if (!blob) { fclose(f); return -ENOMEM; }
    if (fread(blob, 1, sz, f) != (size_t)sz) {
        fclose(f); free(blob); return -EIO;
    }
    fclose(f);

    /* Send in 4 kB chunks (max USB bulk packet for HS/SS) */
    const int CHUNK = 4096;
    long offset = 0;
    int ret = 0;
    while (offset < sz) {
        int n = (int)min_t(long, CHUNK, sz - offset);
        /* Prepend scatter DMA header: [seq(1)] [len(2)] [addr(4)] */
        u8 hdr[8];
        hdr[0] = MCU_CMD_FW_SCATTER;
        hdr[1] = (u8)(n & 0xff);
        hdr[2] = (u8)((n >> 8) & 0xff);
        u32 addr = cpu_to_le32((u32)offset);
        memcpy(hdr + 4, &addr, 4);

        u8 *chunk_buf = (u8 *)malloc(8 + n);
        memcpy(chunk_buf, hdr, 8);
        memcpy(chunk_buf + 8, blob + offset, n);

        ret = usb_bulk_out(d, chunk_buf, 8 + n);
        free(chunk_buf);
        if (ret < 0) break;
        offset += n;
    }

    free(blob);
    LOGI("FW blob %s: uploaded %ld/%ld bytes  rc=%d", path, offset, sz, ret);
    return (ret < 0) ? ret : 0;
}

int mt7921_fw_upload(mt7921_dev_t *dev, const char *rom_path, const char *patch_path) {
    int r;
    LOGI("Uploading patch firmware: %s", patch_path);
    r = upload_blob(dev, patch_path);
    if (r < 0) { LOGE("Patch upload failed: %d", r); return r; }

    /* After patch upload the chip needs ~50 ms to apply it */
    usleep(50000);

    LOGI("Uploading ROM firmware: %s", rom_path);
    r = upload_blob(dev, rom_path);
    if (r < 0) { LOGE("ROM upload failed: %d", r); return r; }

    usleep(200000);   /* wait for firmware to boot */
    dev->fw_loaded = 1;
    LOGI("Firmware upload complete");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 4 – Monitor mode                                                */
/* ─────────────────────────────────────────────────────────────────────── */

int mt7921_set_monitor_mode(mt7921_dev_t *dev, int enable) {
    /*
     * mt7921 monitor mode MCU payload (8 bytes):
     *   [0]    = enable (1) / disable (0)
     *   [1..7] = reserved zeros
     * Source: drivers/net/wireless/mediatek/mt76/mt7921/mcu.c
     *         mt7921_mcu_set_sniffer()
     */
    u8 payload[8] = { (u8)(enable ? 1 : 0), 0,0,0,0,0,0,0 };
    int r = mt7921_mcu_send_cmd(dev, MCU_EXT_CMD_MONITOR, payload, sizeof(payload));
    if (r < 0) {
        LOGE("set_monitor_mode(%d) failed: %d", enable, r);
        return r;
    }
    LOGI("Monitor mode %s", enable ? "ENABLED" : "disabled");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 5 – Channel switch (incl. 6 GHz + DE country code)             */
/* ─────────────────────────────────────────────────────────────────────── */

/**
 * Channel frequency helpers
 */
static int chan_to_freq_khz(int ch) {
    if (ch >= 1 && ch <= 13)
        return (2412 + (ch - 1) * 5) * 1000;           /* 2.4 GHz */
    if (ch >= 36 && ch <= 177)
        return (5180 + (ch - 36) * 5) * 1000;          /* 5 GHz   */
    /* 6 GHz: channel 1 → 5955 MHz, step = 5 MHz × ch_number */
    if (ch >= 1 && ch <= 233)
        return (5955 + ch * 5) * 1000;                 /* 6 GHz   */
    return 0;
}

static int is_6ghz(int ch) {
    return (ch >= 1 && ch <= 233 && ch % 4 == 1); /* rough heuristic */
}

int mt7921_set_channel(mt7921_dev_t *dev, int channel, int bandwidth) {
    /*
     * Channel switch MCU payload (from mt7921_mcu_set_chan_info()):
     *  [0..1]  control_channel (LE16)
     *  [2]     center_channel
     *  [3]     tx_streams
     *  [4]     rx_streams
     *  [5]     band (0=2.4G 1=5G 2=6G)
     *  [6]     width (0=20 1=40 2=80 3=160)
     *  [7..10] center_freq_1 (LE32)  kHz
     *  [11]    country[0]  'D'
     *  [12]    country[1]  'E'
     *  [13]    reserved
     */
    u8 band = 0;
    if (channel >= 36 && channel <= 177) band = 1;
    else if (is_6ghz(channel))           band = 2;

    u32 freq = cpu_to_le32((u32)chan_to_freq_khz(channel));
    u16 ctrl = cpu_to_le16((u16)channel);

    u8 payload[14];
    memset(payload, 0, sizeof(payload));
    memcpy(payload + 0, &ctrl, 2);
    payload[2] = (u8)channel;
    payload[3] = 2;               /* tx streams */
    payload[4] = 2;               /* rx streams */
    payload[5] = band;
    payload[6] = (u8)bandwidth;
    memcpy(payload + 7, &freq, 4);

    /* Country code DE – tells the firmware to allow 6 GHz */
    payload[11] = 'D';
    payload[12] = 'E';

    int r = mt7921_mcu_send_cmd(dev, MCU_EXT_CMD_CHANNEL_SWITCH, payload, sizeof(payload));
    if (r < 0) {
        LOGE("set_channel(%d bw=%d) failed: %d", channel, bandwidth, r);
        return r;
    }
    dev->channel = channel;
    LOGI("Tuned to channel %d (%s, bw=%d)",
         channel,
         band == 2 ? "6 GHz" : band == 1 ? "5 GHz" : "2.4 GHz",
         bandwidth);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 6 – PCAP TCP server                                             */
/* ─────────────────────────────────────────────────────────────────────── */

static int pcap_server_init(mt7921_dev_t *dev) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { LOGE("socket: %s", strerror(errno)); return -1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(PCAP_TCP_PORT);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("bind :%d failed: %s", PCAP_TCP_PORT, strerror(errno));
        close(srv);
        return -1;
    }
    listen(srv, 1);
    dev->pcap_sock = srv;
    LOGI("PCAP server listening on 127.0.0.1:%d", PCAP_TCP_PORT);
    return 0;
}

static int pcap_accept_client(mt7921_dev_t *dev) {
    LOGI("Waiting for client on :%d …", PCAP_TCP_PORT);
    dev->client_sock = accept(dev->pcap_sock, NULL, NULL);
    if (dev->client_sock < 0) {
        LOGE("accept failed: %s", strerror(errno));
        return -1;
    }
    LOGI("Client connected to PCAP stream");

    /* Send pcap global header (LINKTYPE_IEEE802_11_RADIOTAP = 127) */
    pcap_global_hdr_t gh = {
        .magic         = 0xa1b2c3d4,
        .version_major = 2,
        .version_minor = 4,
        .thiszone      = 0,
        .sigfigs       = 0,
        .snaplen       = 65535,
        .network       = 127
    };
    write(dev->client_sock, &gh, sizeof(gh));
    return 0;
}

static void pcap_send_frame(mt7921_dev_t *dev, const u8 *frame, int len) {
    if (dev->client_sock < 0) return;

    /* Minimal 8-byte Radiotap header */
    radiotap_hdr_t rh = {
        .it_version = 0,
        .it_pad     = 0,
        .it_len     = cpu_to_le16(8),
        .it_present = cpu_to_le32(0)
    };

    struct timeval tv; gettimeofday(&tv, NULL);
    pcap_rec_hdr_t rech = {
        .ts_sec   = (u32)tv.tv_sec,
        .ts_usec  = (u32)tv.tv_usec,
        .incl_len = (u32)(sizeof(rh) + len),
        .orig_len = (u32)(sizeof(rh) + len)
    };

    /* Atomic write: rec_hdr + radiotap + 802.11 frame */
    struct iovec iov[3] = {
        { &rech, sizeof(rech) },
        { &rh,   sizeof(rh)   },
        { (void *)frame, (size_t)len }
    };
    writev(dev->client_sock, iov, 3);
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 7 – RX loop                                                     */
/* ─────────────────────────────────────────────────────────────────────── */

void mt7921_rx_loop(mt7921_dev_t *dev) {
    /* Start PCAP TCP server and wait for first client */
    if (pcap_server_init(dev) < 0) return;
    if (pcap_accept_client(dev) < 0) return;

    u8 *rxbuf = (u8 *)malloc(RX_BUF_SIZE);
    if (!rxbuf) return;

    LOGI("RX loop started on CH %d", dev->channel);

    while (dev->running) {
        int actual = 0;
        int r = usb_bulk_in(dev, rxbuf, RX_BUF_SIZE, &actual);

        if (r == -ETIMEDOUT) continue;   /* normal – no packet within timeout */
        if (r < 0) {
            LOGE("RX bulk error %d – retrying in 100 ms", r);
            usleep(100000);
            continue;
        }
        if (actual < 4) continue;         /* too short to be a valid frame */

        /*
         * mt7921 RX descriptor format (simplified):
         *  [0..3]   RXD0: bit 11..0 = packet_type, bit 15..12 = band
         *  [4..7]   RXD1: bit 15..0 = payload_length
         *  [8..n]   802.11 MPDU (may include FCS)
         *
         * We skip the 16-byte HW RX descriptor and hand the 802.11
         * payload directly to the PCAP layer.
         */
        const int RXD_LEN = 16;
        if (actual <= RXD_LEN) continue;

        u8  *mac_frame = rxbuf + RXD_LEN;
        int  mac_len   = actual - RXD_LEN;

        pcap_send_frame(dev, mac_frame, mac_len);
        LOGD("RX %d bytes → PCAP", mac_len);
    }

    free(rxbuf);
    LOGI("RX loop exited");
}

/* ─────────────────────────────────────────────────────────────────────── */
/* SECTION 8 – Open / Close                                                */
/* ─────────────────────────────────────────────────────────────────────── */

mt7921_dev_t *mt7921_open(int android_usb_fd) {
    mt7921_dev_t *dev = (mt7921_dev_t *)calloc(1, sizeof(*dev));
    if (!dev) return NULL;

    dev->client_sock = -1;
    dev->pcap_sock   = -1;
    dev->running     = 1;

    libusb_init(&dev->usb_ctx);
    libusb_set_option(dev->usb_ctx, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);

    /*
     * libusb_wrap_sys_device():
     * This is the *critical* Android integration point.
     * Instead of opening the USB device ourselves (which requires
     * root), we accept the FD that Android's UsbManager already
     * opened with the user's permission.
     */
    int r = libusb_wrap_sys_device(dev->usb_ctx, (intptr_t)android_usb_fd, &dev->dev);
    if (r != LIBUSB_SUCCESS || !dev->dev) {
        LOGE("libusb_wrap_sys_device failed: %s", libusb_strerror((libusb_error)r));
        libusb_exit(dev->usb_ctx);
        free(dev);
        return NULL;
    }

    /* Claim interface 0 */
    libusb_claim_interface(dev->dev, 0);

    LOGI("mt7921_open OK (FD=%d)", android_usb_fd);
    return dev;
}

void mt7921_close(mt7921_dev_t *dev) {
    if (!dev) return;
    dev->running = 0;
    if (dev->client_sock >= 0) close(dev->client_sock);
    if (dev->pcap_sock   >= 0) close(dev->pcap_sock);
    if (dev->dev)   libusb_release_interface(dev->dev, 0);
    if (dev->dev)   libusb_close(dev->dev);
    if (dev->usb_ctx) libusb_exit(dev->usb_ctx);
    free(dev);
    LOGI("mt7921_close done");
}
