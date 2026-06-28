# Ntel: Intel Xe-LP macOS Compatibility Layer

[![macOS](https://img.shields.io/badge/macOS-Ventura+-blue.svg)](https://www.apple.com/macos/ventura)
[![Platform](https://img.shields.io/badge/Platform-Intel%2012th%20Gen-orange.svg)](#)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

**Ntel** is a high-performance, hybrid translation and spoofing framework designed to bring hardware-accelerated Metal support to 12th Gen Intel (Alder Lake) iGPUs, specifically targeting the **Intel Core i7/i5-1235U** (Iris Xe architecture).

---

> [!WARNING]
> **This is an engineering proof-of-concept.** It is intended for research and development purposes. Using custom kernel extensions can lead to system instability, kernel panics, and data loss. **Use at your own risk.**

---

## 🎯 Current Status

- **Target GPU**: Intel Core i5/i7-1235U (Alder Lake-U, Iris Xe Graphics)
- **Spoof Target**: AMD Radeon Pro 5500M  <!-- device ID 0x7340 -->
- **Status**: Experimental. Core functionality is in place, but stability may vary.

## 🚀 Quick Start Guide (For Users)

If you just want to get graphics acceleration working on your 1235U, follow these steps.

The following kexts must be present in your EFI before proceeding:
- `Lilu.kext`
- `WhateverGreen.kext`

### 1. OpenCore Requirements

Your OpenCore configuration must be prepared to handle the spoofed identity and bypass Apple's security checks.

**Required Boot-Args:**
Add the following to your `boot-args` in your `config.plist`:

```
amfi=0x80 agdpmod=pikera
```

- `amfi=0x80`: Disables AMFI enforcement to allow loading unsigned DriverKit extensions.
- `agdpmod=pikera`: Prevents black-screen issues common with AMD spoofing (requires WhateverGreen).

**Required DeviceProperties:**
Inject the following into your `DeviceProperties` to trick macOS into seeing an AMD Radeon Pro 5500M:

> Before using this path, confirm it on your board:
>   gfxutil -f GFX0
> or:
>   ioreg -l | grep -i igpu

- **Path**: `PciRoot(0x0)/Pci(0x2,0x0)`
- **Properties**:
  - `device-id`: `<data>40730000</data>`
  - `vendor-id`: `<data>02100000</data>`
  <!-- AAPL,ig-platform-id REMOVED. Setting it to 00000000 does NOT disable
       the iGPU — it sets the Intel framebuffer platform ID to entry 0.
       Omit this key entirely when doing AMD identity spoofing. -->

> [!IMPORTANT]
> Use EITHER OpenCore DeviceProperties OR NtelSpoofKext — NOT both.
> If you configured device-id/vendor-id above, do NOT load NtelSpoofKext.

### 2. Deployment

1.  Download the latest **Ntel MacOS Production Bundle**.
2.  Copy the bundle to your macOS drive.
3.  Open Terminal and run the deployment orchestrator:
    ```bash
    cd NtelMacOS_Production
    sudo ./deploy-dev.sh
    ```
4.  Reboot your system.

### 🛠️ Troubleshooting Matrix

| Symptom                                         | Likely Diagnosis               | The Fix                                                                                                             |
| :---------------------------------------------- | :----------------------------- | :------------------------------------------------------------------------------------------------------------------ |
| **Immediate Kernel Panic on boot**              | AMFI or SIP is still active.   | Verify `boot-args` in NVRAM and ensure `amfi=0x80` is present.                                       |
| **System boots, but graphics freeze after ~2s** | Asynchronous Watchdog Trip.    | The command stream is stalling. Check logs for `NtelWatchdog` alerts; ensure firmware is correctly loaded.          |
| **Visual artifacts / "Bleeding" textures**      | Context Isolation Failure.     | The `_activeContextID` failed to reset. Check for conflicting third-party kexts interfering with memory management. |
| **"Command not found" during deployment**       | Missing execution permissions. | Run `chmod +x deploy-dev.sh`.                                                                                       |

---

## 🧪 How It Works

Ntel uses a "translation sandwich" approach to enable the Intel iGPU without rewriting the entire graphics stack. It's built on four architectural pillars:

1.  **PCIe Identity Spoofing**: A supervisor kext (`NtelSpoofKext`) intercepts the `IOPCIDevice` probe to replace the Intel PCI ID (`0x46A8`) with an AMD Radeon identity. This forces macOS to load the native `AMDRadeonX6000` drivers.
2.  **High-Speed IPC Ring**: A lock-free, VA-invariant circular buffer (`NtelSharedRing`) allows for extremely fast communication between the user-space DriverKit extension and the kernel-space kext.
3.  **Real-Time ISA Translation**: A DriverKit extension (`NtelShaderChannel.dext`) intercepts the Metal command stream (in Apple Intermediate Representation, or AIR), and translates it on-the-fly into Intel Gen12-compatible bytecode.
4.  **Firmware Injection**: The kext handles loading the required `GuC` (Graphics Microcontroller) and `HuC` (HEVC Microcontroller) firmware blobs into the GPU to initialize its command submission and media pipelines.

For a deeper technical explanation of the memory model, ISA translation, and security protocols, please see the **Architecture Deep Dive**.

---

## 🛠️ Building From Source

We welcome contributions! To get started, you'll need a standard macOS development environment with the Kernel Development Kit (KDK).

### Firmware Sourcing

> [!WARNING]
> **Firmware loading is not yet implemented.** The linux-firmware blobs
> (guc_xe_lp.bin / huc_xe_lp.bin) are ELF-wrapped for the Linux kernel's
> request_firmware() API. macOS has no equivalent loading pathway and no code
> in this project uses these files. Do not source or place them; the build
> does not need them and doing so gives false confidence in unbuilt features.

For detailed build instructions, debugging protocols, and contribution guidelines, please refer to the **Development & Engineering Guide**.

---

## ⚖️ License

This project is released under the MIT License. See the LICENSE file for details.
