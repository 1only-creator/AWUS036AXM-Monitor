# AWUS036AXM WiFi Monitor Mode – Android App (no root)

Userspace 802.11 monitor mode driver for the **Alfa Network AWUS036AXM**
(MediaTek MT7921AU, WiFi 6E / AX1800) running on Android **without root**.

---

## How it works (architecture)

```
┌────────────────────────────────────────────────────────────┐
│  Android UsbManager  (Java/Kotlin)                        │
│  ↳ requests USB permission from the user                  │
│  ↳ opens device → hands fileDescriptor to native layer    │
└──────────────────────────┬─────────────────────────────────┘
                           │  int fd  (via JNI)
┌──────────────────────────▼─────────────────────────────────┐
│  libusb_wrap_sys_device(ctx, fd, &handle)                  │
│  ← this is the key trick: libusb reuses Android's FD      │
│     instead of trying to open /dev/bus/usb itself          │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌──────────────────────────▼─────────────────────────────────┐
│  mt7921_shim.cpp  (userspace "driver")                     │
│                                                            │
│  mt7921_fw_upload()     → sends firmware blobs via USB     │
│  mt7921_set_monitor_mode() → MCU EXT_CMD 0x34             │
│  mt7921_set_channel()   → MCU EXT_CMD 0x22 + DE country  │
│  mt7921_rx_loop()       → bulk-IN → radiotap → TCP :37008 │
└──────────────────────────┬─────────────────────────────────┘
                           │  pcap-over-TCP
┌──────────────────────────▼─────────────────────────────────┐
│  Termux / Wireshark / tcpdump                              │
│  nc 127.0.0.1 37008 | wireshark -k -i -                   │
└────────────────────────────────────────────────────────────┘
```

---

## Prerequisites

| Tool | Version |
|------|---------|
| Android Studio | Hedgehog 2023.1+ |
| Android NDK | r26+ |
| CMake | 3.22+ |
| Target Android | API 26+ (Android 8.0) |
| libusb | 1.0.27 (clone into `app/src/main/cpp/libusb/`) |

---

## Build steps

### 1 – Clone libusb into the project

```bash
cd app/src/main/cpp
git clone https://github.com/libusb/libusb.git
```

Copy `libusb/android/config.h` to `libusb/config.h`:

```bash
cp libusb/android/config.h libusb/config.h
```

### 2 – Get the firmware blobs

The MT7921AU needs two proprietary MediaTek firmware files.
They are distributed with the Linux kernel firmware package and
are free to redistribute alongside open-source drivers.

**Option A – extract from a Linux system:**
```bash
ls /lib/firmware/mediatek/
# copy these two:
# WIFI_RAM_CODE_MT7961_1.bin
# WIFI_MT7961_patch_mcu_1_2_hdr.bin
```

**Option B – download from linux-firmware:**
```bash
git clone --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git
ls linux-firmware/mediatek/WIFI*MT7961*
```

Place both files in `app/src/main/assets/`:
```
app/src/main/assets/
  WIFI_RAM_CODE_MT7961_1.bin
  WIFI_MT7961_patch_mcu_1_2_hdr.bin
```

### 3 – Build & install

```bash
./gradlew installDebug
```

---

## Usage

### On the phone

1. Plug the AWUS036AXM into your phone via a **USB-C OTG adapter**.
2. The app launches automatically (or open it manually).
3. Select a channel in the dropdown (default: 6 GHz CH 37).
4. Tap **START** → grant USB permission when Android asks.
5. The app starts monitor mode and listens on `127.0.0.1:37008`.

### Capture with Termux

```bash
# Install packages once:
pkg install tcpdump netcat-openbsd

# Capture to file:
nc 127.0.0.1 37008 > capture.pcap
# (Ctrl-C to stop, then open capture.pcap in Wireshark on a PC)

# OR live in Termux:
nc 127.0.0.1 37008 | tcpdump -r - -n
```

### Capture with Wireshark (on the same Android device via X11/Termux-X11)

```bash
nc 127.0.0.1 37008 | wireshark -k -i -
```

### Capture forwarded to a PC (via adb)

```bash
# On PC:
adb forward tcp:37008 tcp:37008
nc 127.0.0.1 37008 | wireshark -k -i -
```

---

## 6 GHz (WiFi 6E) notes

* The MT7921AU supports 6 GHz in Europe (DE regulatory domain).
* The shim sends country code **DE** in the channel-switch MCU command,
  which unlocks the 6 GHz band in the firmware.
* Valid 6 GHz monitor channels: 1, 5, 9, 13, 17, 21, 25, 29, 33, 37 …
  (every 4th channel starting at 1, corresponding to 5955–6415 MHz).
* If your phone's OTG power budget is insufficient the adapter may drop
  off the bus. Use a powered USB hub or a USB-C hub with PD pass-through.

---

## Project structure

```
AWUS036AXM-Monitor/
├── app/src/main/
│   ├── AndroidManifest.xml
│   ├── assets/                    ← put firmware blobs here
│   │   ├── WIFI_RAM_CODE_MT7961_1.bin
│   │   └── WIFI_MT7961_patch_mcu_1_2_hdr.bin
│   ├── java/com/example/wifimonitor/
│   │   ├── MainActivity.kt        ← USB permission + UI
│   │   ├── DriverForegroundService.kt
│   │   ├── NativeBridge.kt        ← JNI declarations
│   │   └── FirmwareManager.kt     ← copies blobs from assets
│   ├── cpp/
│   │   ├── CMakeLists.txt
│   │   ├── compat.h               ← "fake kernel" shim header
│   │   ├── mt7921_shim.h          ← public C API
│   │   ├── mt7921_shim.cpp        ← userspace driver implementation
│   │   ├── native_bridge.cpp      ← JNI entry points
│   │   └── libusb/                ← clone here (see Build steps)
│   └── res/
│       ├── layout/activity_main.xml
│       └── xml/usb_device_filter.xml
└── README.md
```

---

## Known limitations / TODO

| Item | Status |
|------|--------|
| TX / injection | Not implemented (RX only) |
| Channel hopping | Manual via UI only |
| HT40 / VHT80 / HE80 | Coded but not validated |
| WPA3 / FILS frames | Captured but not decoded by shim |
| Hotspot mode while capturing | Not possible (one radio) |
| AFC for 6 GHz standard power | Not implemented (only low-power indoor) |

---

## Legal

This software is for **educational and authorised security testing** only.
Monitor mode packet capture may be illegal without permission from the
network owner.  The firmware blobs are the property of MediaTek and are
distributed under their standard Linux firmware licence.
