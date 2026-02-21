/**
 * mt7921_shim.h
 * =============
 * Public API of the userspace MT7921AU driver shim.
 *
 * Architecture overview
 * ---------------------
 *
 *  ┌──────────────────────────────────────────────────────────┐
 *  │  Java / Kotlin layer                                     │
 *  │  UsbManager  →  UsbDeviceConnection  →  fileDescriptor  │
 *  └────────────────────────┬─────────────────────────────────┘
 *                           │  FD  (via JNI)
 *  ┌────────────────────────▼─────────────────────────────────┐
 *  │  native_bridge.cpp  (JNI glue)                           │
 *  └────────────────────────┬─────────────────────────────────┘
 *                           │
 *  ┌────────────────────────▼─────────────────────────────────┐
 *  │  mt7921_shim.cpp  (this layer)                           │
 *  │                                                          │
 *  │  libusb_wrap_sys_device()  ←  Android FD                │
 *  │  mt7921_fw_upload()        ←  firmware blob from assets │
 *  │  mt7921_set_monitor_mode()                               │
 *  │  mt7921_set_channel()      ←  channel + 6 GHz country   │
 *  │  RX loop:  libusb bulk-in  →  radiotap header           │
 *  │                            →  TCP socket :37008         │
 *  └──────────────────────────────────────────────────────────┘
 *
 * Packet consumer (Termux / Wireshark)
 * ------------------------------------
 *   nc 127.0.0.1 37008 | wireshark -k -i -
 *
 * All 802.11 frames are prepended with an 8-byte minimal Radiotap
 * header so Wireshark / tcpdump understand the link-layer type
 * (LINKTYPE_IEEE802_11_RADIOTAP = 127).
 */
#pragma once

#include "compat.h"
#include <libusb.h>

/* ── MT7921AU USB identifiers ─────────────────────────────────────────── */
#define MT7921AU_VID   0x0e8d
#define MT7921AU_PID   0x7961

/* ── MT7921 MCU command codes (from mt76 source) ──────────────────────── */
#define MCU_CMD_FW_SCATTER      0x10
#define MCU_CMD_INIT_ACCESS_REG 0x3
#define MCU_EXT_CMD_PHY_SETTING 0x76
#define MCU_EXT_CMD_SET_RADAR   0x56
#define MCU_EXT_CMD_MONITOR     0x34   /* enable/disable monitor mode */
#define MCU_EXT_CMD_CHANNEL_SWITCH 0x22

/* ── PCAP-over-TCP port ───────────────────────────────────────────────── */
#define PCAP_TCP_PORT 37008

/* ── Firmware blob paths (copied from APK assets at runtime) ─────────── */
#define FW_PATH_ROM   "/data/data/com.example.wifimonitor/files/WIFI_RAM_CODE_MT7961_1.bin"
#define FW_PATH_PATCH "/data/data/com.example.wifimonitor/files/WIFI_MT7961_patch_mcu_1_2_hdr.bin"

/* ── Internal driver context ─────────────────────────────────────────── */
typedef struct {
    libusb_context       *usb_ctx;
    libusb_device_handle *dev;
    int                   channel;       /* 802.11 channel number   */
    volatile int          running;       /* 0 = stop RX loop        */
    int                   pcap_sock;     /* TCP server accept FD    */
    int                   client_sock;   /* accepted client FD      */
    u8                    fw_loaded;
} mt7921_dev_t;

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * mt7921_open - initialise libusb using an existing Android USB FD.
 * Returns NULL on error.
 */
mt7921_dev_t *mt7921_open(int android_usb_fd);

/**
 * mt7921_fw_upload - load ROM and patch firmware blobs onto the chip.
 * Must be called before any MCU command.
 * Returns 0 on success, negative errno on failure.
 */
int mt7921_fw_upload(mt7921_dev_t *dev,
                     const char *rom_path,
                     const char *patch_path);

/**
 * mt7921_set_monitor_mode - switch the radio to (or from) monitor mode.
 * In monitor mode all 802.11 frames are passed to the host; association
 * is impossible.  Pass enable=0 to return to managed mode.
 */
int mt7921_set_monitor_mode(mt7921_dev_t *dev, int enable);

/**
 * mt7921_set_channel - tune the radio to a specific 802.11 channel.
 *
 * For 6 GHz channels (≥ 1 and using 20 MHz spacing at 5955 + ch*5 MHz)
 * the function also sends the country-code (DE) to the firmware so that
 * AFC restrictions do not block the band.
 *
 * @channel: channel number (1-13 2.4 GHz, 36-165 5 GHz, 1-233 6 GHz)
 * @bandwidth: 0=20MHz 1=40MHz 2=80MHz 3=160MHz
 */
int mt7921_set_channel(mt7921_dev_t *dev, int channel, int bandwidth);

/**
 * mt7921_rx_loop - blocking receive loop.
 * Reads bulk-IN frames, prepends a Radiotap header, and forwards them to
 * the PCAP TCP socket on port 37008.  Returns when dev->running == 0.
 */
void mt7921_rx_loop(mt7921_dev_t *dev);

/**
 * mt7921_close - stop the driver and free all resources.
 */
void mt7921_close(mt7921_dev_t *dev);
