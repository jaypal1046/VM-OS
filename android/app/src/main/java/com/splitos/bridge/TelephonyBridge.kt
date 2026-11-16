package com.splitos.bridge

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.os.BatteryManager
import android.os.Build
import android.telephony.PhoneStateListener
import android.telephony.SmsManager
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.splitos.jni.VmJniBridge

/**
 * SplitOS Telephony & Native IPC Bridge.
 * Intercepts guest virtual UART commands (CALL, SMS, NOTIF, SET_MODE)
 * and dispatches host phone events (Battery, Call Ringing, Network) back into guest OS.
 */
class TelephonyBridge(private val context: Context) : VmJniBridge.GuestIpcListener {

    companion object {
        private const val TAG = "TelephonyBridge"
        private const val CHANNEL_ID = "splitos_guest_alerts"
        private const val CHANNEL_NAME = "SplitOS Guest Alerts"
    }

    interface ModeChangeListener {
        fun onModeChanged(isDesktop: Boolean)
    }

    var modeChangeListener: ModeChangeListener? = null

    private val appContext = context.applicationContext
    private var isListening = false
    private var batteryReceiver: BroadcastReceiver? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var telephonyManager: TelephonyManager? = null

    fun start() {
        if (isListening) return
        isListening = true

        VmJniBridge.setGuestIpcListener(this)
        createNotificationChannel()
        registerBatteryMonitor()
        registerNetworkMonitor()
        registerPhoneStateMonitor()

        Log.i(TAG, "TelephonyBridge started successfully.")
    }

    fun stop() {
        if (!isListening) return
        isListening = false

        VmJniBridge.setGuestIpcListener(null)
        unregisterBatteryMonitor()
        unregisterNetworkMonitor()

        Log.i(TAG, "TelephonyBridge stopped.")
    }

    override fun onGuestIpcMessage(command: String) {
        val trimmed = command.trim()
        if (trimmed.isEmpty()) return

        try {
            when {
                trimmed.startsWith("CALL:") -> {
                    val number = trimmed.removePrefix("CALL:").trim()
                    handleCallRequest(number)
                }
                trimmed.startsWith("SMS:") -> {
                    val parts = trimmed.removePrefix("SMS:").split(":", limit = 2)
                    if (parts.isNotEmpty()) {
                        val number = parts[0].trim()
                        val message = if (parts.size > 1) parts[1].trim() else ""
                        handleSmsRequest(number, message)
                    }
                }
                trimmed.startsWith("NOTIF:") -> {
                    val parts = trimmed.removePrefix("NOTIF:").split(":", limit = 2)
                    val title = if (parts.isNotEmpty()) parts[0].trim() else "SplitOS Alert"
                    val content = if (parts.size > 1) parts[1].trim() else ""
                    handleNotificationRequest(title, content)
                }
                trimmed.startsWith("SET_MODE:") -> {
                    val mode = trimmed.removePrefix("SET_MODE:").trim().lowercase()
                    val isDesktop = mode == "desktop"
                    modeChangeListener?.onModeChanged(isDesktop)
                }
                else -> {
                    Log.d(TAG, "Unhandled guest command: $trimmed")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error handling guest IPC command '$trimmed': ${e.message}", e)
        }
    }

    private fun handleCallRequest(number: String) {
        Log.i(TAG, "Guest OS requested call to: $number")
        val cleanNumber = number.replace("[^0-9+*#]".toRegex(), "")
        if (cleanNumber.isEmpty()) return

        val hasCallPermission = ContextCompat.checkSelfPermission(
            appContext,
            android.Manifest.permission.CALL_PHONE
        ) == PackageManager.PERMISSION_GRANTED

        val intent = if (hasCallPermission) {
            Intent(Intent.ACTION_CALL, Uri.parse("tel:$cleanNumber")).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
        } else {
            // Safe fallback to dialer without requiring CALL_PHONE runtime permission
            Intent(Intent.ACTION_DIAL, Uri.parse("tel:$cleanNumber")).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
        }
        appContext.startActivity(intent)
    }

    private fun handleSmsRequest(number: String, message: String) {
        Log.i(TAG, "Guest OS requested SMS to $number: $message")
        val cleanNumber = number.replace("[^0-9+]".toRegex(), "")
        if (cleanNumber.isEmpty()) return

        val hasSmsPermission = ContextCompat.checkSelfPermission(
            appContext,
            android.Manifest.permission.SEND_SMS
        ) == PackageManager.PERMISSION_GRANTED

        if (hasSmsPermission) {
            try {
                @Suppress("DEPRECATION")
                val smsManager = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    appContext.getSystemService(SmsManager::class.java)
                } else {
                    SmsManager.getDefault()
                }
                smsManager.sendTextMessage(cleanNumber, null, message, null, null)
                Log.i(TAG, "SMS dispatched successfully to $cleanNumber")
                return
            } catch (e: Exception) {
                Log.w(TAG, "Failed direct SMS dispatch, falling back to intent: ${e.message}")
            }
        }

        // Fallback: Launch native SMS compose intent
        val intent = Intent(Intent.ACTION_SENDTO, Uri.parse("smsto:$cleanNumber")).apply {
            putExtra("sms_body", message)
            flags = Intent.FLAG_ACTIVITY_NEW_TASK
        }
        appContext.startActivity(intent)
    }

    private fun handleNotificationRequest(title: String, content: String) {
        val notificationManager = appContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val notification = NotificationCompat.Builder(appContext, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle(title)
            .setContentText(content)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .build()

        notificationManager.notify(System.currentTimeMillis().toInt(), notification)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Alerts generated by SplitOS guest applications"
            }
            val notificationManager = appContext.getSystemService(NotificationManager::class.java)
            notificationManager?.createNotificationChannel(channel)
        }
    }

    private fun registerBatteryMonitor() {
        batteryReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                if (intent == null) return
                val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
                val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
                val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)

                val pct = if (level >= 0 && scale > 0) ((level.toFloat() / scale.toFloat()) * 100).toInt() else 100
                val isCharging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
                val chargingBit = if (isCharging) 1 else 0

                // Broadcast EVENT:BATTERY:<pct>:<is_charging>
                sendHostEvent("EVENT:BATTERY:$pct:$chargingBit")
            }
        }

        val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        appContext.registerReceiver(batteryReceiver, filter)
    }

    private fun unregisterBatteryMonitor() {
        batteryReceiver?.let {
            try {
                appContext.unregisterReceiver(it)
            } catch (e: Exception) {
                Log.w(TAG, "Error unregistering battery receiver: ${e.message}")
            }
            batteryReceiver = null
        }
    }

    private fun registerNetworkMonitor() {
        try {
            connectivityManager = appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            val request = NetworkRequest.Builder()
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build()

            networkCallback = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    sendHostEvent("EVENT:NETWORK:CONNECTED")
                }

                override fun onLost(network: Network) {
                    sendHostEvent("EVENT:NETWORK:DISCONNECTED")
                }
            }
            connectivityManager?.registerNetworkCallback(request, networkCallback!!)
        } catch (e: Exception) {
            Log.w(TAG, "Unable to register network callback: ${e.message}")
        }
    }

    private fun unregisterNetworkMonitor() {
        networkCallback?.let {
            try {
                connectivityManager?.unregisterNetworkCallback(it)
            } catch (e: Exception) {
                Log.w(TAG, "Error unregistering network callback: ${e.message}")
            }
            networkCallback = null
        }
    }

    private fun registerPhoneStateMonitor() {
        val hasReadPhoneState = ContextCompat.checkSelfPermission(
            appContext,
            android.Manifest.permission.READ_PHONE_STATE
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasReadPhoneState) {
            Log.d(TAG, "READ_PHONE_STATE permission not granted. Skipping phone state listener.")
            return
        }

        telephonyManager = appContext.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager ?: return

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                telephonyManager?.registerTelephonyCallback(
                    appContext.mainExecutor,
                    object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                        override fun onCallStateChanged(state: Int) {
                            handleCallStateChange(state, null)
                        }
                    }
                )
            } else {
                @Suppress("DEPRECATION")
                telephonyManager?.listen(
                    object : PhoneStateListener() {
                        @Deprecated("Deprecated in Java")
                        override fun onCallStateChanged(state: Int, phoneNumber: String?) {
                            handleCallStateChange(state, phoneNumber)
                        }
                    },
                    @Suppress("DEPRECATION") PhoneStateListener.LISTEN_CALL_STATE
                )
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error registering telephony listener: ${e.message}")
        }
    }

    private fun handleCallStateChange(state: Int, phoneNumber: String?) {
        val caller = phoneNumber ?: "Incoming Call"
        when (state) {
            TelephonyManager.CALL_STATE_RINGING -> {
                sendHostEvent("EVENT:CALL_INCOMING:$caller")
                sendHostEvent("EVENT:CALL_RINGING:$caller")
            }
            TelephonyManager.CALL_STATE_OFFHOOK -> {
                sendHostEvent("EVENT:CALL_OFFHOOK:$caller")
            }
            TelephonyManager.CALL_STATE_IDLE -> {
                sendHostEvent("EVENT:CALL_IDLE")
            }
        }
    }

    fun sendHostEvent(event: String) {
        Log.d(TAG, "Sending host event to guest VM: $event")
        VmJniBridge.nativeSendSerialMessage("$event\n")
    }
}
