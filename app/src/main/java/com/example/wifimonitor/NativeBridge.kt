package com.example.wifimonitor

/**
 * NativeBridge
 * ------------
 * Thin Kotlin object that loads the shared library and exposes
 * the JNI functions implemented in native_bridge.cpp.
 */
object NativeBridge {
    init {
        System.loadLibrary("mt7921_driver")   // libmt7921_driver.so
    }

    /**
     * Starts the userspace MT7921 driver.
     * Blocks until [stopDriver] is called or an unrecoverable error occurs.
     *
     * @param usbFd   File descriptor obtained from UsbDeviceConnection.fileDescriptor
     * @param channel 802.11 channel number (1-13 = 2.4 GHz, 36-177 = 5 GHz, 1-233 step 4 = 6 GHz)
     */
    external fun startDriver(usbFd: Int, channel: Int)

    /**
     * Signals the native driver loop to exit cleanly.
     * Safe to call from any thread.
     */
    external fun stopDriver()
}
