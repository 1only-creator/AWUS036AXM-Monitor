Android WiFi Monitor

A minimalist, open-source tool enabling AWUS036AXM (MT7921AU) support on Android devices for WiFi monitoring. No root or Shizuku (ADB) required.

This project utilizes a custom userspace implementation of libusb and linux-firmware to bypass standard Android kernel-level hardware restrictions.

Includes required firmware blobs and build dependencies.


Features

Hardware Support: Specifically optimized for the AWUS036AXM chipset via USB-OTG.

Frequency Range: Full support for 2.4GHz, 5GHz, and 6GHz (Wi-Fi 6E) bands.

Rootless Operation: Functions entirely in userspace; does not require rooting your device or using Shizuku (ADB) workarounds.


Prerequisites

Android device with USB-OTG support

Compatible USB-C to USB-A adapter.

Termux installed for data handling and CLI utilities.


Build & Installation

build the application from source using the Android SDK.

Prepare Dependencies

You need to move the config.h file to the correct directory before building:

Linux / macOS:
Bash

git clone https://github.com/1only-creator/AWUS036AXM-Monitor
cd AWUS036AXM-Monitor/app/src/main/cpp/libusb/android
cp config.h ..

Windows:
Move config.h from:
...\app\src\main\cpp\libusb\android\config.h
to:
...\app\src\main\cpp\libusb\config.h

Build and Install

Linux / macOS:
Bash

./gradlew installDebug

Windows:
PowerShell

.\gradlew.bat installDebug


Project Status

This project is a Work in Progress (WIP).


Legal Disclaimer

This tool is intended for educational purposes and authorized security auditing only. Using this software against networks without explicit permission is illegal.

Germany (§ 202c StGB / "Hackerparagraf"): The developer assumes no liability for any misuse of this tool. It is the sole responsibility of the end-user to obey all applicable local, state, and federal laws. In Germany, the mere possession can be legal for professional use, but use against third parties is strictly prohibited.


Licensing & Credits

This project is licensed under the MIT License. However, it interacts with components under the following licenses:

libusb: Licensed under GNU LGPL v2.1.

linux-firmware: Firmware binaries (specifically for MediaTek MT7921AU) are subject to their respective vendor licenses (GPL or Proprietary Redistribution).

Attribution: Developed with the help of Claude 3.7 Sonnet and Gemini 3.1 Pro.