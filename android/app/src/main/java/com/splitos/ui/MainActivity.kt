package com.splitos.ui

import android.Manifest
import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.splitos.R
import com.splitos.databinding.ActivityMainBinding
import com.splitos.jni.VmJniBridge
import com.splitos.service.VmForegroundService
import java.io.File
import java.io.FileOutputStream

/**
 * SplitOS Main Viewport & Host Controller Activity.
 * Bridges SurfaceView hardware-accelerated rendering, handles touch input,
 * controls VM lifecycle, and toggles Mobile vs. Desktop convergence layouts.
 */
class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "MainActivity"
        private const val GUEST_OS_ASSET = "guest_os.bin"
    }

    private lateinit var binding: ActivityMainBinding
    private var vmService: VmForegroundService? = null
    private var isBound = false
    private var isDesktopMode = false
    private var isTrackpadActive = false

    private val mainHandler = Handler(Looper.getMainLooper())
    private var ramMonitorRunnable: Runnable? = null

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as VmForegroundService.LocalBinder
            vmService = binder.getService()
            isBound = true
            Log.i(TAG, "Bound to VmForegroundService.")
            updateUiState()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            vmService = null
            isBound = false
            Log.i(TAG, "Unbound from VmForegroundService.")
            updateUiState()
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        permissions.forEach { (perm, isGranted) ->
            Log.i(TAG, "Permission $perm: granted=$isGranted")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupSurfaceView()
        setupControls()
        requestSystemPermissions()
        extractGuestOsAssetIfNeeded()
        startRamMonitor()
    }

    override fun onStart() {
        super.onStart()
        val serviceIntent = Intent(this, VmForegroundService::class.java)
        bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    override fun onStop() {
        if (isBound) {
            unbindService(serviceConnection)
            isBound = false
        }
        super.onStop()
    }

    override fun onDestroy() {
        stopRamMonitor()
        super.onDestroy()
    }

    private fun setupSurfaceView() {
        binding.vmSurfaceView.holder.addCallback(this)
        setupTouchListener()
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupTouchListener() {
        binding.vmSurfaceView.setOnTouchListener { view, event ->
            val surfaceWidth = view.width.toFloat()
            val surfaceHeight = view.height.toFloat()

            if (surfaceWidth > 0 && surfaceHeight > 0) {
                val action = when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> VmJniBridge.TOUCH_ACTION_DOWN
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> VmJniBridge.TOUCH_ACTION_UP
                    MotionEvent.ACTION_MOVE -> VmJniBridge.TOUCH_ACTION_MOVE
                    else -> -1
                }

                if (action != -1) {
                    val normX = (event.x / surfaceWidth).coerceIn(0f, 1f)
                    val normY = (event.y / surfaceHeight).coerceIn(0f, 1f)
                    VmJniBridge.nativeSendTouchInput(normX, normY, action)
                }
            }
            true
        }
    }

    private fun setupControls() {
        // Power button: Start / Stop VM
        binding.btnPower.setOnClickListener {
            val currentState = VmJniBridge.nativeGetVmState()
            if (currentState == VmJniBridge.STATE_STOPPED) {
                startVmSubsystem()
            } else {
                stopVmSubsystem()
            }
        }

        // Pause / Resume button
        binding.btnPause.setOnClickListener {
            val currentState = VmJniBridge.nativeGetVmState()
            if (currentState == VmJniBridge.STATE_RUNNING) {
                vmService?.pauseVmExecution() ?: VmJniBridge.nativePauseVm()
            } else if (currentState == VmJniBridge.STATE_PAUSED) {
                vmService?.resumeVmExecution() ?: VmJniBridge.nativeResumeVm()
            }
            updateUiState()
        }

        // UI Mode toggle: Mobile vs Desktop
        binding.btnToggleMode.setOnClickListener {
            isDesktopMode = !isDesktopMode
            val mode = if (isDesktopMode) VmJniBridge.MODE_DESKTOP else VmJniBridge.MODE_MOBILE
            VmJniBridge.nativeSetDisplayMode(mode)
            binding.btnToggleMode.text = if (isDesktopMode) {
                getString(R.string.mode_desktop)
            } else {
                getString(R.string.mode_mobile)
            }
            Log.i(TAG, "Switched display mode to: ${if (isDesktopMode) "Desktop" else "Mobile"}")
        }

        // Virtual Trackpad toggle
        binding.btnVirtualTrackpad.setOnClickListener {
            isTrackpadActive = !isTrackpadActive
            binding.btnVirtualTrackpad.strokeColor = ContextCompat.getColorStateList(
                this,
                if (isTrackpadActive) R.color.accent_blue else R.color.text_secondary
            )
            Log.i(TAG, "Virtual trackpad toggled: $isTrackpadActive")
        }
    }

    private fun startVmSubsystem() {
        val assetPath = extractGuestOsAssetIfNeeded()
        VmJniBridge.nativeInitVm(assetPath)

        val serviceIntent = Intent(this, VmForegroundService::class.java).apply {
            action = VmForegroundService.ACTION_START
        }
        ContextCompat.startForegroundService(this, serviceIntent)
        vmService?.startVmExecution() ?: VmJniBridge.nativeStartVm()

        updateUiState()
    }

    private fun stopVmSubsystem() {
        val serviceIntent = Intent(this, VmForegroundService::class.java).apply {
            action = VmForegroundService.ACTION_STOP
        }
        startService(serviceIntent)
        vmService?.stopVmExecution() ?: VmJniBridge.nativeStopVm()

        updateUiState()
    }

    private fun updateUiState() {
        val state = VmJniBridge.nativeGetVmState()
        when (state) {
            VmJniBridge.STATE_RUNNING -> {
                binding.statusIndicatorDot.setBackgroundResource(R.drawable.circle_green)
                binding.txtStatus.text = "RUNNING"
                binding.txtStatus.setTextColor(ContextCompat.getColor(this, R.color.accent_green))

                binding.btnPower.text = getString(R.string.btn_stop)
                binding.btnPower.backgroundTintList = ContextCompat.getColorStateList(this, R.color.accent_red)

                binding.btnPause.isEnabled = true
                binding.btnPause.text = getString(R.string.btn_pause)
            }
            VmJniBridge.STATE_PAUSED -> {
                binding.statusIndicatorDot.setBackgroundResource(R.drawable.circle_amber)
                binding.txtStatus.text = "PAUSED"
                binding.txtStatus.setTextColor(ContextCompat.getColor(this, R.color.accent_amber))

                binding.btnPower.text = getString(R.string.btn_stop)
                binding.btnPower.backgroundTintList = ContextCompat.getColorStateList(this, R.color.accent_red)

                binding.btnPause.isEnabled = true
                binding.btnPause.text = getString(R.string.btn_resume)
            }
            else -> {
                binding.statusIndicatorDot.setBackgroundResource(R.drawable.circle_red)
                binding.txtStatus.text = "STOPPED"
                binding.txtStatus.setTextColor(ContextCompat.getColor(this, R.color.text_primary))

                binding.btnPower.text = getString(R.string.btn_start)
                binding.btnPower.backgroundTintList = ContextCompat.getColorStateList(this, R.color.accent_green)

                binding.btnPause.isEnabled = false
                binding.btnPause.text = getString(R.string.btn_pause)
            }
        }
    }

    private fun startRamMonitor() {
        ramMonitorRunnable = object : Runnable {
            override fun run() {
                val ramBytes = VmJniBridge.nativeGetRamUsage()
                val ramMb = ramBytes.toDouble() / (1024.0 * 1024.0)
                binding.txtRamUsage.text = String.format("RAM: %.1f MB", ramMb)
                updateUiState()
                mainHandler.postDelayed(this, 1000L)
            }
        }
        mainHandler.post(ramMonitorRunnable!!)
    }

    private fun stopRamMonitor() {
        ramMonitorRunnable?.let {
            mainHandler.removeCallbacks(it)
            ramMonitorRunnable = null
        }
    }

    private fun extractGuestOsAssetIfNeeded(): String {
        val destFile = File(filesDir, GUEST_OS_ASSET)
        if (!destFile.exists()) {
            try {
                assets.open(GUEST_OS_ASSET).use { input ->
                    FileOutputStream(destFile).use { output ->
                        input.copyTo(output)
                    }
                }
                Log.i(TAG, "Copied guest OS binary to: ${destFile.absolutePath}")
            } catch (e: Exception) {
                Log.w(TAG, "Could not extract $GUEST_OS_ASSET from assets: ${e.message}")
            }
        }
        return destFile.absolutePath
    }

    private fun requestSystemPermissions() {
        val permissions = mutableListOf(
            Manifest.permission.CALL_PHONE,
            Manifest.permission.SEND_SMS,
            Manifest.permission.READ_PHONE_STATE
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        val neededPermissions = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (neededPermissions.isNotEmpty()) {
            permissionLauncher.launch(neededPermissions.toTypedArray())
        }
    }

    // --- SurfaceHolder.Callback implementation ---

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.i(TAG, "Surface created. Binding native surface.")
        VmJniBridge.nativeSetSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "Surface changed: ${width}x${height}, format: $format")
        VmJniBridge.nativeSetSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.i(TAG, "Surface destroyed. Releasing native surface.")
        VmJniBridge.nativeSetSurface(null)
    }
}
