# SplitOS (VM-OS) - Project Handover Document

> **Document Version:** 1.0.0  
> **Date:** November 16, 2026  
> **Project Root Directory:** `C:\Jay\_Plugin\VM-OS`  
> **Target Audience:** Incoming Developers, AI Agents, and Stakeholders  

---

## 📌 1. Project Vision & Core Concept

**SplitOS** is an embedded virtual operating system subsystem designed to run directly inside a standard Android host app without requiring root permissions. 

### Key Innovations:
1. **The "Split" Architecture**: The Android host maintains ownership of mission-critical hardware (cellular radio, SIM cards, telephony, and SMS). The guest OS executes in an isolated sandbox.
2. **Zero Interruption to Phone Calls**: Incoming calls and messages automatically overlay and ring via Android's native subsystem—the VM is never killed or frozen.
3. **Dual Personality (Mobile + Desktop)**: Supports both touch-friendly fullscreen mobile applications and floating multi-window desktop applications with taskbar and mouse/keyboard navigation.
4. **GitHub-First Distribution**: Pre-compiled release APKs and downloadable guest OS images are built and distributed directly via GitHub Actions and GitHub Releases.

---

## 📂 2. Completed Milestones & Files Created

### A. Repository & Agent Guidelines
* **[README.md](file:///C:/Jay/_Plugin/VM-OS/README.md)**: Public project documentation, feature breakdown, architecture diagram, repository layout, and roadmap.
* **[AGENT_README.md](file:///C:/Jay/_Plugin/VM-OS/AGENT_README.md)**: Strict operational constraints for autonomous coding agents (zero-root rules, telephony priority, memory budgets, and IPC conventions).
* **[docs/ARCHITECTURE.md](file:///C:/Jay/_Plugin/VM-OS/docs/ARCHITECTURE.md)**: Deep technical specification covering the Android `ForegroundService` lifecycle, shared framebuffer graphics pipeline, UART serial commands, and window compositor.
* **[LICENSE](file:///C:/Jay/_Plugin/VM-OS/LICENSE)**: Standard open-source MIT License.

### B. CI/CD & Automated Distribution
* **[.github/workflows/build-apk.yml](file:///C:/Jay/_Plugin/VM-OS/.github/workflows/build-apk.yml)**: Automated GitHub Actions pipeline that triggers on version tags (`v*.*.*`), builds the release APK using JDK 17 & Android NDK, and attaches the binary to GitHub Releases.

### C. Android Host Application Foundation (`android/`)
* **Gradle Configuration**:
  * `settings.gradle.kts`: Configured repository management and module inclusion (`:app`).
  * `build.gradle.kts`: Configured Android Gradle Plugin (8.5.0) and Kotlin (1.9.24).
  * `app/build.gradle.kts`: Configured target/compile SDK 34, C++17 CMake integration, ABI filters (`arm64-v8a`, `x86_64`), and AndroidX / Material UI dependencies.
* **App Manifest (`AndroidManifest.xml`)**:
  * Configured permissions: `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_SPECIAL_USE`, `POST_NOTIFICATIONS`, `CALL_PHONE`, `READ_PHONE_STATE`, `SEND_SMS`, `WAKE_LOCK`.
  * Registered `MainActivity` as the primary launcher and `VmForegroundService` as the persistent life-support daemon.
* **UI Design & Layout**:
  * `res/values/strings.xml`, `colors.xml`, `themes.xml`: Dark-mode theme, accent color palette, and localized string definitions.
  * `res/drawable/circle_red.xml`: Status dot indicator.
  * `res/layout/activity_main.xml`: Modern dark-mode interface featuring:
    * **Header Bar**: Live status indicator (Stopped / Running / Paused), real-time RAM usage metric, and Mobile/Desktop mode toggle button.
    * **Central Viewport**: Hardware-accelerated `SurfaceView` for 60 FPS VM framebuffer output.
    * **Bottom Action Bar**: Power button (Start/Stop), Pause/Resume snapshot button, and Virtual Trackpad toggle.

### D. Directory Layout Prepared
* `android/app/src/main/java/com/splitos/`:
  * `service/` — Background life-support service
  * `bridge/` — Telephony, SMS, and native Android intent handlers
  * `ui/` — Launcher activity and overlay controllers
  * `jni/` — JNI bindings to native C/C++ engine
* `android/app/src/main/cpp/`:
  * `vm_core/` — Emulation CPU/RAM engine
  * `display/` — Framebuffer blitting via `ANativeWindow`
  * `serial/` — Virtual UART IPC daemon
* `os/`:
  * `kernel/` — Guest kernel entry & hardware initialization
  * `compositor/` — Adaptive Mobile/Desktop window manager
  * `apps/` — Lightweight guest applications

---

## 🔄 3. Communication Protocol (UART Bridge)

The guest OS communicates with host phone features via a virtual serial port using newline-terminated ASCII:

```text
[ Guest OS ] === UART ===> [ C Core ] === JNI ===> [ Android Host ]
```

| Command | Action Handled by Host |
| :--- | :--- |
| `CALL:<number>` | Fires `Intent.ACTION_CALL` to place outgoing cellular call |
| `SMS:<number>:<msg>` | Dispatches SMS via Android `SmsManager` |
| `NOTIF:<title>:<msg>` | Posts native notification to Android shade |
| `SET_MODE:<mobile|desktop>` | Signals host to update viewport scale / trackpad overlay |

---

## 📋 4. Detailed Implementation Roadmap (What We Need to Do)

The following steps define the exact work required to complete the SplitOS subsystem from end to end:

### Step 1: The Kotlin Host Code (Android Layer)
Build out the core Kotlin classes to manage the VM lifecycle and bridge native Android features:
1. **`VmJniBridge.kt`**:
   * Declares native JNI methods: `nativeInitVm(assetPath: String)`, `nativeStartVm()`, `nativeStopVm()`, `nativePauseVm()`, `nativeResumeVm()`, `nativeSetSurface(surface: Surface)`, `nativeSendTouchInput(x: Float, y: Float, action: Int)`, `nativeSetDisplayMode(mode: Int)`.
   * Houses the JNI callback interface for receiving UART serial messages (`onGuestIpcMessage(command: String)`).
2. **`TelephonyBridge.kt`**:
   * Intercepts `CALL:<number>` commands from the guest OS and dispatches `Intent(Intent.ACTION_CALL)` or opens the native phone dialer.
   * Intercepts `SMS:<number>:<msg>` and dispatches SMS via Android's `SmsManager`.
   * Broadcasts host phone events back into the guest OS (`EVENT:BATTERY:<pct>`, `EVENT:CALL_INCOMING:<caller>`, `EVENT:NETWORK:<status>`).
3. **`VmForegroundService.kt`**:
   * Implements a persistent Android `ForegroundService` with an ongoing notification (`SplitOS Subsystem Active`).
   * Ensures the Android OS does not kill or throttle the VM process when phone calls arrive or when the user switches apps.
4. **`MainActivity.kt`**:
   * Connects the `SurfaceView` display using `SurfaceHolder.Callback` to pass the native surface window to C++.
   * Binds the **Start**, **Stop**, and **Pause/Resume** buttons.
   * Toggles between **Mobile Mode** (fullscreen touch) and **Desktop Mode** (windowed with virtual trackpad).
   * Forwards touch and trackpad events directly into the guest OS.

---

### Step 2: The Native C/C++ Engine (`cpp/`)
The high-performance virtualization core compiled into `libvmcore.so`:
1. **`CMakeLists.txt`**:
   * Configures compilation of all C/C++ sources for `arm64-v8a` and `x86_64`.
   * Links required Android system libraries: `libandroid` (for `ANativeWindow`), `liblog` (Android Logcat), and `libjnigraphics`.
2. **`display_bridge.c` / `display_bridge.h`**:
   * Manages the shared 32-bit RGBA linear framebuffer (e.g., 1920x1080).
   * Directly locks and blits pixel data to Android's `ANativeWindow` at 60 FPS without UI-thread overhead.
3. **`uart_ipc.c` / `uart_ipc.h`**:
   * Emulates a bidirectional UART serial device.
   * Buffers text commands from the guest OS and dispatches them across JNI into `TelephonyBridge.kt`.
   * Injects host phone events into the guest OS serial receive buffer.
4. **`vm_engine.c` / `vm_engine.h`**:
   * The micro-VM execution loop: CPU instruction stepping, RAM memory mapping, and timer interrupts.
   * Supports memory snapshots for instant pause and resume functionality.
5. **`jni_bindings.c`**:
   * Maps JNI entry points between Kotlin (`VmJniBridge`) and native C functions.

---

### Step 3: The Guest OS Image & Dual UI (`os/` & `assets/`)
Create the minimal guest operating system payload:
1. **Guest Kernel Entry (`os/kernel/`)**:
   * Initializes display registers, memory tables, and input interrupt handlers.
2. **Adaptive Compositor (`os/compositor/`)**:
   * **Mobile Mode**: Renders a touch-friendly home launcher with large app icons:
     * *Phone Dialer App*: Tapping it sends `CALL:<number>` over the emulated UART.
     * *Notes / Terminal / Settings Apps*.
   * **Desktop Mode**: Renders floating, resizable windows with title bars `[ - ] [ + ] [ X ]`, a bottom taskbar, and mouse pointer tracking.
3. **Image Packaging (`assets/guest_os.bin`)**:
   * Compiles the guest OS into a compact binary image placed in `android/app/src/main/assets/guest_os.bin`.

---

### Step 4: Verification & Integration Testing
1. **Lifecycle Test**: Start VM -> receive incoming phone call -> verify VM continues running uninterrupted in background.
2. **Telephony Bridge Test**: Tap the Dialer app inside the guest OS -> verify Android's native calling screen launches.
3. **Dual Mode Test**: Toggle between Mobile and Desktop modes -> verify UI scales and re-renders smoothly.
4. **GitHub CI/CD Test**: Push a git tag -> verify release APK and guest OS image are built and published automatically.
