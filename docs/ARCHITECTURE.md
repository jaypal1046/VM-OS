# SplitOS System Architecture & Technical Specification

This document describes the target architecture for **SplitOS**. Current implementation runs a host-compiled C compositor in the Android process; it has no guest CPU execution, independent kernel boot, or guest application isolation. The image builder packages metadata, not executable firmware. Pause/resume snapshots exist only in memory. Calls and SMS open Android confirmation screens; direct CALL_PHONE and SEND_SMS permissions are not requested. Memory reporting estimates core state, RAM, snapshot buffers, and framebuffer; it excludes Android/process overhead.

---

## 1. Core Design Goals

1. **Non-Intrusive Virtualization**: Zero root requirements; runs inside standard Android application sandbox boundaries.
2. **True Split Integration**: Android hardware control (cellular baseband, telephony, emergency services, Wi-Fi) remains owned by the host Android OS.
3. **Adaptive Convergence**: Support both single-window mobile touch layouts and multi-window floating desktop applications.
4. **Effortless Lifecycle**: Safe start, background execution, pause/snapshot, and instant shutdown without data corruption.

---

## 2. Component Architecture

```
+--------------------------------------------------------------------------+
|                       ANDROID HOST APPLICATION                            |
|                                                                          |
|  +------------------------+  +-------------------+  +-----------------+  |
|  |    Launcher UI         |  | Foreground Service|  | TelephonyBridge |  |
|  | (Start/Stop/Snapshots) |  | (Keeps VM Alive)  |  | (Calls / SMS)   |  |
|  +------------------------+  +-------------------+  +-----------------+  |
|               |                        |                      |          |
|               +-----------+------------+                      |          |
|                           |                                   |          |
|                           v                                   v          |
|  +-----------------------------------------------+  +-----------------+  |
|  |          Android SurfaceView / OpenGL         |  |   JNI Gateway   |  |
|  +-----------------------------------------------+  +-----------------+  |
+---------------------------|-----------------------------------|----------+
                            |                                   |
                     Raw Pixels                               IPC Events
                            |                                   |
+---------------------------v-----------------------------------v----------+
|                       NATIVE C/C++ VM CORE                               |
|                                                                          |
|  +------------------------+  +-------------------+  +-----------------+  |
|  | Emulated Hardware      |  | Framebuffer Ram   |  | Virtual UART    |  |
|  | (CPU / MMU / Clocks)   |  | (RGBA 60 FPS)     |  | (Serial Port)   |  |
|  +------------------------+  +-------------------+  +-----------------+  |
+---------------------------|-----------------------------------|----------+
                            |                                   |
+---------------------------v-----------------------------------v----------+
|                       SplitOS GUEST ENVIRONMENT                          |
|                                                                          |
|  +--------------------------------------------------------------------+  |
|  |                      Adaptive Compositor                           |  |
|  |       [Mobile Touch Layer]        [Desktop Multi-Window Layer]     |  |
|  +--------------------------------------------------------------------+  |
|  |                 SplitOS Kernel / Standard Library                  |  |
|  +--------------------------------------------------------------------+  |
+--------------------------------------------------------------------------+
```

---

## 3. Host Android Subsystem

### 3.1 Foreground Service & Process Persistence
* Android aggressively terminates background apps when under memory pressure.
* SplitOS runs within an Android `ForegroundService` with a low-overhead ongoing notification (`SplitOS is running`).
* If an incoming telephone call arrives, Android's `InCallService` or default dialer appears on top. The foreground service requests background execution; process survival and call behavior are not guaranteed. Process termination loses in-memory snapshots.

### 3.2 Display Pipeline
1. The guest OS writes to a designated memory region (Linear Framebuffer, e.g. 1920x1080 32-bit RGBA).
2. The NDK layer accesses this buffer via a direct pointer (`ANativeWindow_lock` / `ANativeWindow_unlockAndPost`).
3. Frame synchronization is tied to the Android display VSYNC rate (60Hz / 120Hz).

---

## 4. Telephony & IPC Protocol

The guest OS communicates with host phone features through an emulated **Virtual Serial Interface (UART)**.

### Command Specification

| Direction | Command Format | Action |
| :--- | :--- | :--- |
| **Guest -> Host** | `CALL:<phone_number>` | Host launches native phone dialer / triggers `ACTION_CALL`. |
| **Guest -> Host** | `SMS:<number>:<message>` | Host queues SMS through `SmsManager`. |
| **Guest -> Host** | `NOTIF:<title>:<content>` | Host displays a standard Android notification. |
| **Host -> Guest** | `EVENT:CALL_INCOMING:<caller>` | Guest receives notification banner of incoming caller. |
| **Host -> Guest** | `EVENT:BATTERY:<percent>:<charging>` | Guest updates its status bar battery indicator. |
| **Host -> Guest** | `EVENT:DISPLAY_MODE:<mobile|desktop>` | Guest dynamically adjusts its window manager layout. |

---

## 5. Dual UI: Mobile vs. Desktop Mode

SplitOS includes a unified compositor supporting two operational layouts:

### Mobile Mode
* **Interaction**: Single-touch, swipe navigation.
* **Layout**: Single active application running full-screen.
* **Top Bar**: Minimal clock, virtual battery status, quick-settings toggle.
* **Navigation Bar**: Back, Home, and OS Switcher buttons.

### Desktop Mode
* **Interaction**: Mouse pointer, right-click context menus, physical / virtual keyboard shortcuts.
* **Layout**: Freely draggable and resizable windows with headers (`[ - ] [ + ] [ X ]`).
* **Taskbar**: Running process tray, application menu launcher, and system tray.
* **Trigger**: Automatic switch when HDMI/USB-C display is connected, or via quick-toggle in the Host UI.

---

## 6. Distribution & CI/CD Strategy via GitHub

### Automated GitHub Actions Pipelines
1. **Host APK Build (`build-apk.yml`)**:
   * Triggers on tags matching `v*.*.*`.
   * Sets up Android SDK and NDK.
   * Compiles native C++ libraries for `arm64-v8a` and `x86_64`.
   * Signs the release APK and uploads it to GitHub Releases.

2. **Guest OS Image Build (`build-os-image.yml`)**:
   * Cross-compiles the guest kernel and base userland.
   * Generates compressed disk images (`split-os-rootfs.img.gz`).
   * Attaches pre-packaged system images directly to release tags.

3. **In-App Updater**:
   * The Android Host queries the GitHub REST API (`/repos/{owner}/{repo}/releases/latest`) on startup (if network is enabled).
   * Users can download updated OS images and APK updates directly inside the application.
