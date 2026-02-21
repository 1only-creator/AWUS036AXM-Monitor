package com.example.wifimonitor

import android.content.Context
import android.util.Log
import java.io.File

/**
 * FirmwareManager
 * ---------------
 * On first launch, copies the MT7921AU firmware blobs from the APK's
 * assets/ folder to the app's internal files/ directory, where the
 * native C++ code can open them with fopen().
 *
 * Required assets (obtain from the official MediaTek firmware package
 * or extract from a Linux system's /lib/firmware/mediatek/):
 *
 *   assets/
 *     WIFI_RAM_CODE_MT7961_1.bin        ← main firmware ROM
 *     WIFI_MT7961_patch_mcu_1_2_hdr.bin ← MCU patch
 *
 * Firmware redistribution note:
 * These firmware files are proprietary MediaTek blobs distributed under
 * their standard Linux firmware licence (non-commercial redistribution
 * allowed alongside open-source drivers).  They are already present on
 * every Android device with a MediaTek chip; on non-MediaTek devices you
 * must supply them yourself.
 */
object FirmwareManager {
    private const val TAG = "FirmwareManager"

    private val FIRMWARE_FILES = listOf(
        "WIFI_RAM_CODE_MT7961_1.bin",
        "WIFI_MT7961_patch_mcu_1_2_hdr.bin"
    )

    /**
     * Returns true if all firmware blobs are ready in internal storage.
     * Copies from assets/ if not yet present.
     */
    fun ensureFirmware(ctx: Context): Boolean {
        var allOk = true
        for (name in FIRMWARE_FILES) {
            val dest = File(ctx.filesDir, name)
            if (dest.exists() && dest.length() > 0) {
                Log.d(TAG, "FW already present: ${dest.absolutePath}")
                continue
            }
            try {
                ctx.assets.open(name).use { input ->
                    dest.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                Log.i(TAG, "Copied firmware: $name → ${dest.absolutePath}  (${dest.length()} bytes)")
            } catch (e: Exception) {
                Log.e(TAG, "Failed to copy $name: ${e.message}")
                allOk = false
            }
        }
        return allOk
    }

    fun allPresent(ctx: Context): Boolean =
        FIRMWARE_FILES.all { File(ctx.filesDir, it).let { f -> f.exists() && f.length() > 0 } }

    /** Delete cached blobs (force re-copy on next launch) */
    fun clearCache(ctx: Context) {
        FIRMWARE_FILES.forEach { File(ctx.filesDir, it).delete() }
    }
}
