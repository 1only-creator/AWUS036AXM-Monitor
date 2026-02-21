/**
 * native_bridge.cpp
 * =================
 * JNI entry points called from NativeBridge.kt.
 *
 * Java signature: com.example.wifimonitor.NativeBridge
 *   startDriver(usbFd: Int, channel: Int)
 *   stopDriver()
 */

#include <jni.h>
#include <android/log.h>
#include "mt7921_shim.h"

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "MT7921-JNI", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "MT7921-JNI", __VA_ARGS__)

static mt7921_dev_t *g_dev = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_wifimonitor_NativeBridge_startDriver(
        JNIEnv *env, jobject /*thiz*/,
        jint usbFd, jint channel)
{
    LOGI("startDriver(fd=%d, ch=%d)", usbFd, channel);

    /* 1. Open the MT7921AU chip via the Android-provided USB FD */
    g_dev = mt7921_open((int)usbFd);
    if (!g_dev) {
        LOGE("mt7921_open failed – aborting");
        return;
    }

    /* 2. Upload firmware blobs (they were copied from APK assets by Kotlin) */
    int r = mt7921_fw_upload(g_dev, FW_PATH_ROM, FW_PATH_PATCH);
    if (r < 0) {
        LOGE("Firmware upload failed (%d).  Continuing anyway – device may already be running fw.", r);
        /* Non-fatal: if the chip survived a previous run the FW is still loaded */
    }

    /* 3. Enter monitor (sniffer) mode */
    r = mt7921_set_monitor_mode(g_dev, 1);
    if (r < 0) {
        LOGE("set_monitor_mode failed (%d)", r);
        mt7921_close(g_dev);
        g_dev = nullptr;
        return;
    }

    /* 4. Tune to requested channel (BW=0 → 20 MHz default) */
    r = mt7921_set_channel(g_dev, (int)channel, 0);
    if (r < 0) {
        LOGE("set_channel(%d) failed (%d)", channel, r);
        /* Continue – we are in monitor mode even without a confirmed channel */
    }

    /* 5. Blocking RX loop – forwards frames to PCAP TCP :37008 */
    LOGI("Entering RX loop …");
    mt7921_rx_loop(g_dev);
    LOGI("RX loop returned – cleaning up");

    mt7921_close(g_dev);
    g_dev = nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_wifimonitor_NativeBridge_stopDriver(
        JNIEnv *env, jobject /*thiz*/)
{
    LOGI("stopDriver called");
    if (g_dev) {
        g_dev->running = 0;   /* signals RX loop to exit */
    }
}
