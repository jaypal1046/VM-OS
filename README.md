# SplitOS 📱💻

> **A Hybrid Mobile & Desktop Micro-OS Virtualization Subsystem for Android**

[![GitHub Release](https://img.shields.io/github/v/release/your-username/split-os?include_prereleases&style=flat-square)](https://github.com/your-username/split-os/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square)](LICENSE)
[![Platform: Android](https://img.shields.io/badge/Platform-Android%208.0%2B-green.svg?style=flat-square)](https://developer.android.com)

---

## 🌟 Overview

**SplitOS** is an embedded virtual operating system designed to run directly inside an Android application without requiring root privileges. It provides a flexible **split architecture**:

1. **Host (Android)** handles cellular calls, SMS, notifications, and battery management uninterrupted in the background.
2. **Guest (SplitOS)** runs an isolated, lightweight operating system capable of running both **touch-first mobile apps** and **multi-window desktop apps**.
3. **Seamless Control**: Users can launch, pause, snapshot, and stop the environment on demand just like any standard app or launcher.

---

## 🚀 Key Features

* **Dual Interface (Mobile & Desktop Mode)**:
  * **Mobile Mode**: Fullscreen, touch-friendly UI for simple mobile apps and quick tools.
  * **Desktop Mode**: Multi-window interface with title bars, draggable/resizable windows, taskbar, and virtual trackpad/mouse support.
* **Split Telephony Bridge**:
  * Phone calls and text messages trigger native Android overlays.
  * Incoming calls will **never** be dropped or blocked while inside the guest OS.
  * Guest OS can trigger outgoing calls and SMS via an IPC serial bridge.
* **Zero Root Required**: Runs purely within standard Android user-space using an embedded NDK micro-virtualization engine.
* **Instant Pause & Resume**: Save RAM snapshots to disk on demand so you can stop the OS anytime and resume without losing state.
* **External Display & DeX Integration**: Automatically switch to desktop mode when connected to an external monitor via USB-C or HDMI.
* **GitHub Distribution**: Automated builds for both host APKs and guest OS image releases directly from GitHub Actions.

---

## 🏗️ Architecture

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
- [ ] Minimal NDK Micro-VM Core integration
- [ ] Android Host UI with Start / Stop / State snapshotting
- [ ] Bidirectional IPC bridge for phone call forwarding
- [ ] Adaptive Window Compositor (Mobile full-screen + Desktop floating windows)
- [ ] GitHub Actions release pipeline for continuous APK delivery

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
