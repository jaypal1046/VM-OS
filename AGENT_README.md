# SplitOS - Agent & Developer Instruction Guide (AGENT_README.md)

> **Audience:** Autonomous Coding Agents (Antigravity, Claude, Copilot, etc.) and Core Contributors.  
> **Purpose:** Defines project constraints, architectural boundaries, build procedures, and operational guidelines for AI agents working in this repository.

---

## 🤖 1. Agent Mission & Repository Context

You are developing **SplitOS**, a dual-personality micro-operating system subsystem that runs inside an unrooted Android host application.

### The Core Premise:
* **Host (Android App)**: Acts as the hypervisor manager, display viewport (`SurfaceView`), and communications bridge (Telephony, SMS, Notifications).
* **Core (NDK / C++)**: Manages CPU/RAM virtualization, framebuffer translation, and virtual UART serial IO.
* **Guest (SplitOS)**: An ultra-lightweight environment executing both **touch-first mobile apps** and **multi-window desktop apps**.

---

## 🛡️ 2. Non-Negotiable Constraints for Agents

1. **ZERO ROOT DEPENDENCY**:
   * Never implement solutions requiring `su`, rooting tools, Magisk, or unlocking bootloaders.
   * All virtualization must run in standard Android application user-space permissions.

2. **HOST STABILITY & CALL PRESERVATION**:
   * The Android host's telephony services are paramount. 
   * Never block the main thread or prevent incoming calls/SMS.
   * VM execution MUST be bound to a persistent `ForegroundService`.

3. **MINIMAL FOOTPRINT**:
   * Keep memory overhead low (< 64MB idle RAM for the base VM).
   * Prefer single-file or lightweight C engines (e.g. TinyEMU / mini-rv32ima) over heavy, multi-gigabyte emulation platforms.
   * Avoid large, bloat-heavy third-party dependencies unless strictly justified.

4. **CONVERGENT UI DISCIPLINE**:
   * Every UI module in the guest OS must be designed to adapt to:
     * **Mobile Mode**: Fullscreen, large hit-targets, touch gestures.
     * **Desktop Mode**: Draggable windows, title bars `[ - ] [ + ] [ X ]`, cursor/pointer support.

---

## 📐 3. Module Boundaries & Ownership

```text
split-os/
├── android/            # HOST TIER: Kotlin / Java
│   ├── app/src/main/java/com/splitos/
│   │   ├── service/    # ForegroundService lifecycle & notification handlers
│   │   ├── bridge/     # Telephony, SMS, and native Android IPC bridges
│   │   ├── ui/         # Launcher, overlay controls, settings, SurfaceView
│   │   └── jni/        # JNI interfaces mapping to native VM core
│   └── app/src/main/cpp/
│       ├── vm_core/    # Virtual CPU, MMU, memory allocation
│       ├── display/    # Framebuffer synchronization (ANativeWindow)
│       └── serial/     # Emulated UART for IPC communication
│
├── os/                 # GUEST TIER: C / Rust / Bare-metal or Minimal Linux
│   ├── kernel/         # Guest kernel entry, page tables, hardware traps
│   ├── compositor/     # Window manager (Mobile fullscreen + Desktop windows)
│   ├── apps/           # Built-in lightweight apps (Terminal, Notes, Files)
│   └── lib/            # IPC serial client library (talking to Android host)
│
├── .github/workflows/  # DEPLOYMENT TIER: CI/CD & Releases
└── docs/               # System documentation & specifications
```

---

## 🔄 4. IPC Bridge Protocol Conventions

When extending guest-to-host functionality, use the emulated UART serial port with newline-delimited ASCII commands:

### Guest -> Host Commands:
* `CALL:<phone_number>` -> Triggers Android `Intent(Intent.ACTION_CALL)` or launches dialer.
* `SMS:<phone_number>:<message>` -> Sends SMS via Android `SmsManager`.
* `NOTIF:<title>:<content>` -> Emits host notification via `NotificationManager`.
* `SET_MODE:<mobile|desktop>` -> Informs host to update overlay controls/virtual touchpad.

### Host -> Guest Events:
* `EVENT:CALL_RINGING:<caller_id>` -> Guest renders incoming call overlay.
* `EVENT:BATTERY:<percent>:<is_charging>` -> Guest updates battery status bar.
* `EVENT:RESUME` / `EVENT:PAUSE` -> Guest pauses/resumes background timers and loops.

---

## 🛠️ 5. Build & Validation Commands

### Android Host (Gradle):
```powershell
# From the project root:
cd android

# Compile debug APK
./gradlew assembleDebug

# Compile release APK
./gradlew assembleRelease

# Run unit tests
./gradlew test
```

### Guest OS Image:
```powershell
# From the project root:
cd os

# Build guest OS image using the provided Makefile/CMake
make all
```

---

## 📦 6. GitHub Release & Distribution Rules

* **Automated CI**: Never manually package production APKs; push a version tag (`git tag vX.Y.Z`) to trigger `.github/workflows/build-apk.yml`.
* **OS Image Packaging**: Keep guest OS images bundled or downloadable as separate compressed assets (`split-os-rootfs.img.gz`).
* **Semantic Versioning**: Follow `MAJOR.MINOR.PATCH` for all releases.

---

## 🧭 7. Step-by-Step Task Priority for Agents

When implementing new features, prioritize in this sequence:
1. **Host NDK Surface Bridge**: Ensure 60 FPS pixel rendering to Android's `SurfaceView`.
2. **Foreground Service Daemon**: Guarantee zero background process termination during phone calls.
3. **UART Telephony IPC**: Test that guest OS commands trigger Android phone dialer events.
4. **Compositor Modes**: Implement the toggle between full-screen touch mode and multi-window desktop mode.
5. **App Sandboxing & GitHub CI Release**: Verify end-to-end automated APK and OS image delivery.
