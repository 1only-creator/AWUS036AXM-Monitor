package com.example.wifimonitor

import android.app.*
import android.content.Intent
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat

/**
 * DriverForegroundService
 * -----------------------
 * Keeps the process alive while the native driver runs its USB RX loop.
 * Receives the USB file-descriptor and channel from the Intent extras,
 * then calls the JNI entrypoint on a dedicated thread.
 *
 * Packet output: the native layer writes raw 802.11 frames (radiotap header
 * prepended) to a local TCP socket on port 37008. Any pcap-capable tool on
 * the same Android device can consume this stream.
 */
class DriverForegroundService : Service() {

    companion object {
        private const val TAG = "DriverService"
        private const val NOTIF_CHANNEL = "driver_channel"
        private const val NOTIF_ID = 1
    }

    private var driverThread: Thread? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val fd = intent?.getIntExtra("fd", -1) ?: -1
        val channel = intent?.getIntExtra("channel", 6) ?: 6

        if (fd < 0) {
            Log.e(TAG, "Invalid FD received – stopping service")
            stopSelf()
            return START_NOT_STICKY
        }

        startForeground(NOTIF_ID, buildNotification("Monitor mode active – CH $channel"))

        driverThread = Thread {
            Log.i(TAG, "Starting native MT7921 driver on FD=$fd CH=$channel")
            NativeBridge.startDriver(fd, channel)   // blocks until stopped
            Log.i(TAG, "Native driver exited")
            stopSelf()
        }.also { it.name = "mt7921-driver"; it.start() }

        return START_NOT_STICKY
    }

    override fun onDestroy() {
        NativeBridge.stopDriver()
        driverThread?.join(2000)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(text: String): Notification =
        NotificationCompat.Builder(this, NOTIF_CHANNEL)
            .setContentTitle("AWUS036AXM Monitor Mode")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setOngoing(true)
            .build()

    private fun createNotificationChannel() {
        val chan = NotificationChannel(
            NOTIF_CHANNEL,
            "WiFi Driver",
            NotificationManager.IMPORTANCE_LOW
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(chan)
    }
}
