# SplitOS 📱💻

> **An Experimental Mobile & Desktop Environment for Android**

[![GitHub Release](https://img.shields.io/github/v/release/your-username/split-os?include_prereleases&style=flat-square)](https://github.com/your-username/split-os/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square)](LICENSE)
[![Platform: Android](https://img.shields.io/badge/Platform-Android%208.0%2B-green.svg?style=flat-square)](https://developer.android.com)

---

## 🌟 Overview

**SplitOS** is an experimental OS-style environment running inside an Android application without root privileges. The current runtime executes a native compositor and built-in tools in the host process. It does not emulate a guest CPU, boot an independent kernel, or isolate guest applications. It provides a flexible **split architecture**:

1. **Host (Android)** handles cellular calls, SMS, notifications, and battery management uninterrupted in the background.
2. **Environment (SplitOS)** provides built-in tools through mobile and desktop layouts. Arbitrary Android, Linux, and desktop applications are not supported.
3. **Seamless Control**: Users can launch, pause, snapshot, and stop the environment on demand just like any standard app or launcher.

---

## 🚀 Key Features

* **Dual Interface (Mobile & Desktop Mode)**:
  * **Mobile Mode**: Fullscreen, touch-friendly UI for simple mobile apps and quick tools.
  * **Desktop Mode**: Multi-window interface with title bars, draggable/resizable windows, taskbar, and virtual trackpad/mouse support.
* **Split Telephony Bridge**:
  * Phone calls and text messages trigger native Android overlays.
  * Android retains control of cellular calls; call behavior and background survival require device testing.
  * Guest requests open Android’s dialer or SMS composer for user confirmation. The app does not place calls or send SMS directly.
* **Zero Root Required**: Runs within the standard Android app process using native C rendering.
* **Instant Pause & Resume**: Pause and resume using in-memory snapshots within the same process. Snapshots are not written to disk and cannot recover state after process termination.
* **External Display & DeX Integration**: Planned automatic desktop-mode switching when an external display connects.
* **GitHub Distribution**: Automated builds for both host APKs and guest OS image releases directly from GitHub Actions.

---

## 🏗️ Architecture

The diagram below describes the target architecture. CPU emulation, MMU, an independent guest kernel, and guest application isolation are not implemented. `guest_os.bin` currently contains metadata and padding, not executable firmware.

```
┌─────────────────────────────────────────────────────────────┐
│                    HOST: ANDROID LAYER                      │
│  • Foreground Service (Lifecycle & Background Persistence)  │
│  • TelephonyManager & Notification Listener (Calls / SMS)   │
│  • SurfaceView / OpenGL Renderer (Display Output)           │
│  • Touch / Virtual Trackpad / Keyboard Input Handler        │
├─────────────────────────────────────────────────────────────┤
│                    JNI / NDK BRIDGE                         │
│  • Shared Framebuffer Pipe (Guest -> Android UI)            │
│  • Virtual Serial / VirtIO-Socket IPC Daemon                │
├─────────────────────────────────────────────────────────────┤
│                 EMBEDDED MICRO-VM CORE                      │
│  • Micro-Emulator Engine (RISC-V / ARM64 via Native C/C++)  │
│  • RAM, MMU, and Virtual Hardware Controller                │
├─────────────────────────────────────────────────────────────┤
│                     GUEST: SplitOS                          │
│  • Lightweight Kernel / Micro-kernel                        │
│  • Adaptive Compositor (Touch Apps & Desktop Windows)       │
│  • Core Apps & Utilities (Terminal, Notes, Browser, Media)  │
└─────────────────────────────────────────────────────────────┘
```

For comprehensive technical specifications, read the [Architecture Documentation](docs/ARCHITECTURE.md).

---

## 📁 Repository Layout

```text
split-os/
├── AGENT_README.md              # Instructions & operational guide for AI agents & contributors
├── .github/
│   └── workflows/
│       ├── build-apk.yml        # CI/CD: Builds and publishes the Android Host APK
│       └── build-os-image.yml   # CI/CD: Compiles and packages guest OS binaries
├── android/                     # Android Host Project (Kotlin + NDK)
│   ├── app/
│   │   ├── src/main/java/       # UI, Launcher, Foreground Service, Telephony Bridge
│   │   ├── src/main/cpp/        # JNI Bindings and VM Core Engine
│   │   └── src/main/assets/     # Bundled base OS image & assets
├── os/                          # Guest OS Source
│   ├── kernel/                  # Kernel entry, drivers, memory management
│   ├── ui/                      # Compositor (Mobile & Desktop window manager)
│   └── apps/                    # Core apps (Calculator, Terminal, Editor)
├── docs/                        # Specifications, IPC protocol, design docs
│   └── ARCHITECTURE.md
└── tools/                       # Build scripts, image packaging tools
```

> 🤖 **Working with AI Agents?** Please refer to [AGENT_README.md](AGENT_README.md) for strict architectural boundaries, zero-root constraints, and IPC standards.

---

## 📦 Distribution via GitHub

All releases and updates are handled through GitHub Releases:

1. **Android Host APK**:
   * Pre-built APKs are published with every tag in [Releases](https://github.com/your-username/split-os/releases).
   * Supports in-app update checks using the GitHub Releases API.
2. **Guest OS Images (`.img` / `.bin`)**:
   * Versioned OS system images and software bundles are hosted as release assets.
   * Users can download new OS packages inside the host app without reinstalling the APK.

---

## 🛠️ Getting Started

### Prerequisites
* **Android Studio Ladybug (or newer)**
* **Android NDK** (version 26+)
* **CMake** (3.22+)
* **Cross-Compiler Toolchain** (e.g., `riscv64-unknown-elf-gcc` or `aarch64-linux-gnu-gcc` if building custom guest kernel)

### Building the Host App
```bash
# Clone the repository
git clone https://github.com/your-username/split-os.git
cd split-os/android

# Build Debug APK
./gradlew assembleDebug
```

---

## 🗺️ Roadmap

- [x] Architecture design & system documentation
- [x] Native compositor/runtime integration (`libvmcore.so`)
- [ ] Guest CPU emulation, kernel boot, and application isolation
- [ ] Persistent snapshots and recovery after process termination
- [x] Android Host UI with Start / Stop / State snapshotting
- [x] Bidirectional IPC bridge for phone call & SMS forwarding
- [x] Adaptive Window Compositor (Mobile full-screen + Desktop floating windows)
- [x] GitHub Actions release pipeline for continuous APK & OS image delivery
- [ ] USB-C / External Display auto-detection with DeX convergence
- [ ] In-App GitHub Releases Updater & Image Downloader
- [ ] Hardware-accelerated OpenGL ES 3.0 graphics passthrough

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
