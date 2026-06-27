# Ntel: Development & Engineering Guide

This document provides the technical requirements, build workflows, and debugging protocols for developers contributing to the Ntel project.

---

## 🛠️ Development Environment Setup

To build and modify the Ntel framework, your development machine must meet the following specifications:

### 1. Required Toolchain
* **macOS Host**: A machine running macOS (Ventura or newer recommended).
* **Xcode**: Latest stable version.
* **macOS Kernel Development Kit (KDK)**: You **MUST** install the KDK that matches the exact version of the macOS target you intend to run on. This provides the necessary headers for kernel-mode development.
* **LLVM-SPIRV**: Required for the translation of Apple Intermediate Representation (AIR) to SPIR-V bytecode.
* **Build Tools**: `make`, `cmake`, and `clang`.

### 2. DriverKit Entitlements & Code Signing
Developing `.dext` (DriverKit) extensions is strictly controlled by Apple.

* **Local Testing (Ad-Hoc)**: For development and testing on machines with SIP/AMFI disabled, you can use ad-hoc signing:
  ```bash
  codesign --force --deep --sign - --entitlements ./NtelShaderChannel.entitlements NtelShaderChannel.dext
  ```
* **Production Signing**: For distribution, a valid Apple Developer Program membership is required to obtain the `com.apple.developer.driverkit` and `com.apple.developer.driverkit.transport.pci` entitlements.

---

## 🏗️ Build Workflow

### 1. Compiling the Simulation Suite
Before testing on real hardware, always verify your changes using the deterministic simulation environment:
```bash
cd NtelSpoofKext
gcc -g -I./include ./src/NtelSharedRing.c ./src/NtelTranslationEngine.c ./src/NtelSimulation.c -o sim_test -lpthread
./sim_test
```

### 2. Compiling the Kext & Dext
The project is structured to be built via `xcodebuild`.
* **To build the Supervisor Kext**:
  ```bash
  xcodebuild -target NtelMacOSKext -configuration Release
  ```
* **To build the Shader Dext**:
  ```bash
  xcodebuild -target NtelShaderChannelDext -configuration Release
  ```

---

## 🐞 Debugging Protocols

### 1. Local Logic Debugging
Use the internal debug macros to increase logging granularity without impacting the performance of the real-time translation engine.
* Edit `NtelSharedRing.h` to adjust `NTEL_LOG_LEVEL`.

### 2. Kernel-Level Debugging (Two-Machine Setup)
For debugging kernel panics or race conditions in the `NtelSpoofService`, a two-machine setup is highly recommended:
* **Target Machine**: The machine running the Intel 1235U.
* **Host Machine**: A machine running `lldb` connected via Ethernet or Thunderbolt.
* **Setup**: Enable KDP (Kernel Debugging Protocol) in the target machine's boot-args.

### 3. Common Failure Modes
| Failure | Debugging Step |
| :--- | :--- |
| **DriverKit Crash** | Check `log stream --process NtelShaderChannel`. |
| **Kernel Panic (Ring Buffer)** | Inspect `NtelSharedRing` offsets. Ensure `writeIdx` is not overtaking `readIdx`. |
| **GPU Hang** | Check for missing `CLFLUSH` or `MFENCE` instructions in the command stream. |

---

## 🤝 Contributing

We welcome contributions to the translation engine, ISA mapping tables, and firmware injection routines. Please ensure all new code is accompanied by a corresponding test case in the `NtelSimulation` suite.
