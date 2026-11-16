package com.splitos.jni

import android.util.Log
import android.view.Surface

/**
 * SplitOS Native VM JNI Gateway.
 * Provides the direct bridge between Android Kotlin host and libvmcore.so.
 */
object VmJniBridge {
    private const val TAG = "VmJniBridge"

    const val MODE_MOBILE = 0
    const val MODE_DESKTOP = 1

    const val STATE_STOPPED = 0
    const val STATE_RUNNING = 1
    const val STATE_PAUSED = 2

    // Touch actions matching Android MotionEvent
    const val TOUCH_ACTION_DOWN = 0
    const val TOUCH_ACTION_UP = 1
    const val TOUCH_ACTION_MOVE = 2

    interface GuestIpcListener {
        fun onGuestIpcMessage(command: String)
    }

    private var ipcListener: GuestIpcListener? = null

    init {
        try {
            System.loadLibrary("vmcore")
            Log.i(TAG, "libvmcore.so loaded successfully.")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load libvmcore.so: ${e.message}")
        }
    }

    fun setGuestIpcListener(listener: GuestIpcListener?) {
        this.ipcListener = listener
    }

    /**
     * Callback triggered directly from native virtual UART IPC (C thread).
     */
    @JvmStatic
    fun onGuestIpcMessage(command: String) {
        ipcListener?.onGuestIpcMessage(command)
    }

    // --- Native JNI Interface Methods ---

    external fun nativeInitVm(assetPath: String): Boolean
    external fun nativeStartVm(): Boolean
    external fun nativeStopVm(): Boolean
    external fun nativePauseVm(): Boolean
    external fun nativeResumeVm(): Boolean
    external fun nativeSetSurface(surface: Surface?): Boolean
    external fun nativeSendTouchInput(x: Float, y: Float, action: Int)
    external fun nativeSetDisplayMode(mode: Int)
    external fun nativeGetDisplayMode(): Int
    external fun nativeSendSerialMessage(command: String)
    external fun nativeGetVmState(): Int
    external fun nativeGetRamUsage(): Long
}
