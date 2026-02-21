package com.example.wifimonitor

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat

/**
 * MainActivity
 * -----------
 * 1. Detects the AWUS036AXM via UsbManager
 * 2. Requests USB permission (Android mandatory step)
 * 3. Passes the file-descriptor to DriverForegroundService
 * 4. The native service starts monitor mode and opens a local TCP socket
 *    on port 37008 (pcap-over-IP), which Termux / tcpdump / Wireshark can
 *    consume via: nc 127.0.0.1 37008 | wireshark -k -i -
 */
class MainActivity : AppCompatActivity() {

    companion object {
        const val ACTION_USB_PERMISSION = "com.example.wifimonitor.USB_PERMISSION"
        // MT7921AU USB IDs
        const val MT7921AU_VID = 0x0e8d
        val MT7921AU_PIDS = intArrayOf(0x7961, 0x7962)
    }

    private lateinit var usbManager: UsbManager
    private var targetDevice: UsbDevice? = null

    // Status views
    private lateinit var tvStatus: TextView
    private lateinit var tvLog: TextView
    private lateinit var btnStartStop: Button
    private lateinit var spinnerChannel: Spinner
    private var driverRunning = false

    private val usbPermissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            if (intent.action == ACTION_USB_PERMISSION) {
                val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                    intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                else
                    @Suppress("DEPRECATION") intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)

                if (granted && device != null) {
                    log("✅ USB permission granted for ${device.productName}")
                    startNativeDriver(device)
                } else {
                    log("❌ USB permission DENIED. Grant it via the popup and try again.")
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        usbManager = getSystemService(USB_SERVICE) as UsbManager
        tvStatus = findViewById(R.id.tvStatus)
        tvLog = findViewById(R.id.tvLog)
        btnStartStop = findViewById(R.id.btnStartStop)
        spinnerChannel = findViewById(R.id.spinnerChannel)

        // Populate channel list (2.4 GHz 1-13, 5 GHz 36-177, 6 GHz 1-233 step 4)
        val channels = buildList {
            (1..13).forEach { add("2.4 GHz CH $it") }
            listOf(36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,
                149,153,157,161,165).forEach { add("5 GHz CH $it") }
            (1..233 step 4).forEach { add("6 GHz CH $it") }
        }
        spinnerChannel.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, channels)
            .also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }

        // Select 6 GHz CH 37 (a common 6 GHz channel) by default
        val defaultIdx = channels.indexOfFirst { it.contains("6 GHz CH 37") }
        if (defaultIdx >= 0) spinnerChannel.setSelection(defaultIdx)

        btnStartStop.setOnClickListener {
            if (driverRunning) stopNativeDriver()
            else checkAndRequestUsb()
        }

        // Register USB permission receiver
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        ContextCompat.registerReceiver(this, usbPermissionReceiver, filter,
            ContextCompat.RECEIVER_NOT_EXPORTED)

        // Handle auto-launch from USB_DEVICE_ATTACHED intent
        if (intent?.action == "android.hardware.usb.action.USB_DEVICE_ATTACHED") {
            log("🔌 Device attached event – scanning USB bus…")
            checkAndRequestUsb()
        } else {
            log("ℹ️  Plug in your AWUS036AXM then tap START, or it triggers automatically.")
        }
    }

    private fun checkAndRequestUsb() {
        val device = findAdapter()
        if (device == null) {
            log("❌ AWUS036AXM not found on USB bus. Is it plugged in?")
            updateStatus("No device found", false)
            return
        }
        targetDevice = device
        log("🔍 Found: ${device.productName}  VID=${device.vendorId.toString(16)} PID=${device.productId.toString(16)}")

        if (usbManager.hasPermission(device)) {
            log("✅ Permission already held")
            startNativeDriver(device)
        } else {
            log("⏳ Requesting USB permission…")
            val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                PendingIntent.FLAG_MUTABLE else 0
            val pi = PendingIntent.getBroadcast(this, 0,
                Intent(ACTION_USB_PERMISSION), flags)
            usbManager.requestPermission(device, pi)
        }
    }

    private fun findAdapter(): UsbDevice? =
        usbManager.deviceList.values.firstOrNull { dev ->
            dev.vendorId == MT7921AU_VID && dev.productId in MT7921AU_PIDS
        }

    private fun startNativeDriver(device: UsbDevice) {
        val connection = usbManager.openDevice(device) ?: run {
            log("❌ openDevice() returned null – try again"); return
        }
        val fd = connection.fileDescriptor
        log("📡 Opening FD=$fd  →  launching native MT7921 driver…")

        val channel = parseChannel(spinnerChannel.selectedItem.toString())
        log("📻 Monitor mode target: ch=$channel")

        val svc = Intent(this, DriverForegroundService::class.java).apply {
            putExtra("fd", fd)
            putExtra("channel", channel)
        }
        ContextCompat.startForegroundService(this, svc)
        driverRunning = true
        btnStartStop.text = "STOP"
        updateStatus("Monitor mode active – ch $channel", true)
        log("🟢 Driver service started. Connect via:\n   nc 127.0.0.1 37008 | wireshark -k -i -\n   or in Termux: tcpdump -r /proc/self/fd/0 < /dev/tcp/127.0.0.1/37008")
    }

    private fun stopNativeDriver() {
        stopService(Intent(this, DriverForegroundService::class.java))
        driverRunning = false
        btnStartStop.text = "START"
        updateStatus("Stopped", false)
        log("🔴 Driver stopped.")
    }

    /** Parses "6 GHz CH 37" → 37 */
    private fun parseChannel(label: String): Int =
        label.substringAfterLast("CH ").trim().toIntOrNull() ?: 6

    private fun updateStatus(msg: String, active: Boolean) {
        tvStatus.text = msg
        tvStatus.setTextColor(
            if (active) getColor(android.R.color.holo_green_dark)
            else getColor(android.R.color.holo_red_dark))
    }

    private fun log(msg: String) {
        runOnUiThread {
            tvLog.append("$msg\n")
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        unregisterReceiver(usbPermissionReceiver)
    }
}
