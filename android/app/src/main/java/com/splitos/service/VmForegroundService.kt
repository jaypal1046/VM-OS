package com.splitos.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import com.splitos.R
import com.splitos.bridge.TelephonyBridge
import com.splitos.jni.VmJniBridge
import com.splitos.ui.MainActivity

/**
 * SplitOS Persistent Life-Support Daemon.
 * Runs as a foreground service to ensure Android does not kill or throttle
 * the VM execution process during telephone calls, SMS events, or app switching.
 */
class VmForegroundService : Service() {

    companion object {
        private const val TAG = "VmForegroundService"
        private const val NOTIFICATION_ID = 1001
        private const val CHANNEL_ID = "splitos_vm_daemon"
        private const val CHANNEL_NAME = "SplitOS Background Daemon"

        const val ACTION_START = "com.splitos.action.START"
        const val ACTION_STOP = "com.splitos.action.STOP"
        const val ACTION_PAUSE = "com.splitos.action.PAUSE"
        const val ACTION_RESUME = "com.splitos.action.RESUME"
    }

    inner class LocalBinder : Binder() {
        fun getService(): VmForegroundService = this@VmForegroundService
    }

    private val binder = LocalBinder()
    private var wakeLock: PowerManager.WakeLock? = null
    private var telephonyBridge: TelephonyBridge? = null
    private var isVmStarted = false

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "Creating VmForegroundService...")
        createNotificationChannel()
        acquireWakeLock()
        telephonyBridge = TelephonyBridge(applicationContext).apply {
            start()
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action ?: ACTION_START
        Log.i(TAG, "onStartCommand with action: $action")

        val notification = buildOngoingNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
                )
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        when (action) {
            ACTION_START -> startVmExecution()
            ACTION_STOP -> stopVmExecution()
            ACTION_PAUSE -> pauseVmExecution()
            ACTION_RESUME -> resumeVmExecution()
        }

        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        Log.i(TAG, "Destroying VmForegroundService...")
        stopVmExecution()
        telephonyBridge?.stop()
        releaseWakeLock()
        super.onDestroy()
    }

    fun startVmExecution(): Boolean {
        if (!isVmStarted) {
            isVmStarted = VmJniBridge.nativeStartVm()
            Log.i(TAG, "VM start execution result: $isVmStarted")
        }
        return isVmStarted
    }

    fun stopVmExecution(): Boolean {
        if (isVmStarted) {
            VmJniBridge.nativeStopVm()
            isVmStarted = false
            Log.i(TAG, "VM execution stopped.")
        }
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        return true
    }

    fun pauseVmExecution(): Boolean {
        Log.i(TAG, "Pausing VM execution snapshot...")
        return VmJniBridge.nativePauseVm()
    }

    fun resumeVmExecution(): Boolean {
        Log.i(TAG, "Resuming VM execution from snapshot...")
        return VmJniBridge.nativeResumeVm()
    }

    fun getRamUsageBytes(): Long = VmJniBridge.nativeGetRamUsage()

    fun getVmState(): Int = VmJniBridge.nativeGetVmState()

    private fun acquireWakeLock() {
        if (wakeLock == null) {
            val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = powerManager.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK,
                "SplitOS::VmLifecycleWakeLock"
            ).apply {
                setReferenceCounted(false)
                acquire(24 * 60 * 60 * 1000L) // Safe 24-hr max limit
            }
            Log.d(TAG, "Partial WakeLock acquired.")
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let {
            if (it.isHeld) {
                it.release()
                Log.d(TAG, "Partial WakeLock released.")
            }
            wakeLock = null
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Keeps SplitOS micro-kernel safely active in the background"
                setShowBadge(false)
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(channel)
        }
    }

    private fun buildOngoingNotification(): Notification {
        val launchIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_lock_idle_charging)
            .setContentTitle(getString(R.string.vm_running_title))
            .setContentText(getString(R.string.vm_running_desc))
            .setOngoing(true)
            .setContentIntent(pendingIntent)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .build()
    }
}
