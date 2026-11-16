package com.splitos.ui

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.splitos.R
import com.splitos.databinding.ActivityMainBinding
import com.splitos.jni.VmJniBridge
import com.splitos.service.VmForegroundService
import com.splitos.updater.UpdateManager
import kotlinx.coroutines.launch
import java.io.File

/**
 * SplitOS Main Viewport & Host Controller Activity.
 * Bridges SurfaceView hardware-accelerated rendering, handles touch input & virtual trackpad,
 * controls VM lifecycle, auto-detects external displays (DeX/HDMI), and checks for updates.
 */
class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var binding: ActivityMainBinding
    private var isTrackpadActive = false

    // Virtual Trackpad relative pointer tracking
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var virtualPointerX = 0.5f
    private var virtualPointerY = 0.5f

    private val mainHandler = Handler(Looper.getMainLooper())
    private var ramMonitorRunnable: Runnable? = null

    private lateinit var displayManager: DisplayManager
    private var displayListener: DisplayManager.DisplayListener? = null
    private val updateManager by lazy { UpdateManager(this) }

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
        setupExternalDisplayMonitor()
        requestSystemPermissions()
        startRamMonitor()
        checkForUpdatesSilently()
    }

    override fun onDestroy() {
        stopRamMonitor()
        displayListener?.let { displayManager.unregisterDisplayListener(it) }
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
                if (isTrackpadActive) {
                    // Virtual Trackpad: Relative pointer motion
                    when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN -> {
                            lastTouchX = event.x
                            lastTouchY = event.y
                            VmJniBridge.nativeSendTouchInput(virtualPointerX, virtualPointerY, VmJniBridge.TOUCH_ACTION_DOWN)
                        }
                        MotionEvent.ACTION_MOVE -> {
                            val dx = (event.x - lastTouchX) / surfaceWidth
                            val dy = (event.y - lastTouchY) / surfaceHeight
                            virtualPointerX = (virtualPointerX + dx * 1.5f).coerceIn(0f, 1f)
                            virtualPointerY = (virtualPointerY + dy * 1.5f).coerceIn(0f, 1f)
                            lastTouchX = event.x
                            lastTouchY = event.y
                            VmJniBridge.nativeSendTouchInput(virtualPointerX, virtualPointerY, VmJniBridge.TOUCH_ACTION_MOVE)
                        }
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                            VmJniBridge.nativeSendTouchInput(virtualPointerX, virtualPointerY, VmJniBridge.TOUCH_ACTION_UP)
                        }
                    }
                } else {
                    // Direct Absolute Touch
                    val action = when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN -> VmJniBridge.TOUCH_ACTION_DOWN
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> VmJniBridge.TOUCH_ACTION_UP
                        MotionEvent.ACTION_MOVE -> VmJniBridge.TOUCH_ACTION_MOVE
                        else -> -1
                    }

                    if (action != -1) {
                        val normX = (event.x / surfaceWidth).coerceIn(0f, 1f)
                        val normY = (event.y / surfaceHeight).coerceIn(0f, 1f)
                        virtualPointerX = normX
                        virtualPointerY = normY
                        VmJniBridge.nativeSendTouchInput(normX, normY, action)
                    }
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
                startService(Intent(this, VmForegroundService::class.java).apply {
                    action = VmForegroundService.ACTION_PAUSE
                })
            } else if (currentState == VmJniBridge.STATE_PAUSED) {
                startService(Intent(this, VmForegroundService::class.java).apply {
                    action = VmForegroundService.ACTION_RESUME
                })
            }
            updateUiState()
        }

        // UI Mode toggle: Mobile vs Desktop
        binding.btnToggleMode.setOnClickListener {
            switchDisplayMode(VmJniBridge.nativeGetDisplayMode() != VmJniBridge.MODE_DESKTOP)
        }

        // Virtual Trackpad toggle
        binding.btnVirtualTrackpad.setOnClickListener {
            isTrackpadActive = !isTrackpadActive
            binding.btnVirtualTrackpad.strokeColor = ContextCompat.getColorStateList(
                this,
                if (isTrackpadActive) R.color.accent_blue else R.color.text_secondary
            )
            val msg = if (isTrackpadActive) "Virtual Trackpad Active" else "Direct Touch Active"
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
            Log.i(TAG, "Virtual trackpad toggled: $isTrackpadActive")
        }
    }

    private fun switchDisplayMode(desktop: Boolean) {
        val mode = if (desktop) VmJniBridge.MODE_DESKTOP else VmJniBridge.MODE_MOBILE
        VmJniBridge.nativeSetDisplayMode(mode)
        updateUiState()
    }

    private fun setupExternalDisplayMonitor() {
        displayManager = getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        displayListener = object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(displayId: Int) {
                Log.i(TAG, "External display connected: ID $displayId")
                runOnUiThread {
                    Toast.makeText(this@MainActivity, "External Display Detected: Switching to Desktop Mode", Toast.LENGTH_SHORT).show()
                    switchDisplayMode(true)
                }
            }

            override fun onDisplayRemoved(displayId: Int) {
                Log.i(TAG, "External display removed: ID $displayId")
                runOnUiThread {
                    val displays = displayManager.displays
                    val hasSecondary = displays.any { it.displayId != Display.DEFAULT_DISPLAY }
                    if (!hasSecondary) {
                        Toast.makeText(this@MainActivity, "External Display Disconnected: Returning to Mobile Mode", Toast.LENGTH_SHORT).show()
                        switchDisplayMode(false)
                    }
                }
            }

            override fun onDisplayChanged(displayId: Int) {}
        }
        displayManager.registerDisplayListener(displayListener, mainHandler)

        // Check if secondary display is already connected on startup
        val displays = displayManager.displays
        if (displays.any { it.displayId != Display.DEFAULT_DISPLAY }) {
            Log.i(TAG, "Secondary display detected on boot, activating Desktop Mode")
            switchDisplayMode(true)
        }
    }

    private fun checkForUpdatesSilently() {
        lifecycleScope.launch {
            updateManager.checkForUpdates(object : UpdateManager.UpdateCallback {
                override fun onUpdateAvailable(info: UpdateManager.ReleaseInfo) {
                    Log.i(TAG, "New SplitOS update found on GitHub: ${info.tagName}")
                }
                override fun onNoUpdateAvailable() {
                    Log.d(TAG, "SplitOS is up to date.")
                }
                override fun onDownloadProgress(percent: Int) {}
                override fun onUpdateComplete(file: File) {
                    Log.i(TAG, "SplitOS updated: ${file.absolutePath}")
                }
                override fun onError(message: String) {
                    Log.d(TAG, "Update check notice: $message")
                }
            })
        }
    }

    private fun startVmSubsystem() {
        val serviceIntent = Intent(this, VmForegroundService::class.java).apply {
            action = VmForegroundService.ACTION_START
        }
        ContextCompat.startForegroundService(this, serviceIntent)

        updateUiState()
    }

    private fun stopVmSubsystem() {
        val serviceIntent = Intent(this, VmForegroundService::class.java).apply {
            action = VmForegroundService.ACTION_STOP
        }
        startService(serviceIntent)

        updateUiState()
    }

    private fun updateUiState() {
        binding.btnToggleMode.text = getString(
            if (VmJniBridge.nativeGetDisplayMode() == VmJniBridge.MODE_DESKTOP) R.string.mode_desktop
            else R.string.mode_mobile
        )
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

    private fun requestSystemPermissions() {
        val permissions = mutableListOf(
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
