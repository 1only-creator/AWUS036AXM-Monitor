Android WiFi Monitor (Rootless)

A minimalist, open-source tool enabling AWUS036AXM (MT7921AU) support on Android devices for WiFi monitoring and deauthentication—no root or ADB required.

This project utilizes a custom userspace implementation of libusb and linux-firmware to bypass standard Android kernel-level hardware restrictions.

!Includes required firmware blobs and build dependencies

🚀 Features

    Hardware Support: Specifically optimized for the AWUS036AXM chipset via USB-OTG.

    Frequency Range: Full support for 2.4GHz, 5GHz, and 6GHz (Wi-Fi 6E) bands.

    Rootless Operation: Functions entirely in userspace; does not require rooting your device or using ADB workarounds.

    Traffic Analysis:

        Stream live traffic via nc (Netcat) to tcpdump.

        View live captures directly in Termux.

        Export and analyze PCAP files via Wireshark.

    Development: Vibecoded with the assistance of Claude 3.7 Sonnet & Gemini 3.1 Pro (2026).

🛠 Prerequisites

    Android device with USB-OTG support (Tested on GrapheneOS/Pixel).

    Compatible USB-C to USB-A adapter.

    Termux installed for data handling and CLI utilities.

    AWUS036AXM Wi-Fi Adapter.

🏗 Build & Installation

Since pre-compiled APKs are not provided at this stage, you must build the application from source using the latest Android SDK.
Bash

# Clone the repository
git clone https://github.com/1only-creator/AWUS036AXM-Monitor
cd AWUS036AXM-Monitor

# Build and install via CLI
./gradlew installDebug 2>&1 | tee build.log

📈 Project Status

    [!IMPORTANT]

    This project is a Work in Progress (WIP).

    Currently, only WiFi Monitoring is functional. Deauthentication features are still under active development.

⚖️ Legal Disclaimer

This tool is intended for educational purposes and authorized security auditing only. Using this software against networks without explicit permission is illegal.

Germany (§ 202c StGB): The developer assumes no liability for any misuse of this tool. It is the end-user's responsibility to obey all applicable local, state, and federal laws.
📚 Licensing & Credits

This project is licensed under the MIT License. However, it incorporates or interacts with components under the following licenses:

    libusb: Licensed under GNU LGPL v2.1. This project links to libusb for userspace USB communication.

    linux-firmware: Firmware binaries (specifically for MediaTek MT7921AU) are subject to their respective vendor licenses (GPL or Proprietary Redistribution).

    Attribution: This software includes code/logic developed with the help of Claude 3.7 Sonnet and Gemini 3.1 Pro.