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
- **Spoof Target**: Intel Ice Lake Gen11 iGPU  <!-- device ID 0x8A52, platform 0x5A08 -->
- **Status**: Experimental. Core spoofing is implemented; translation engine and firmware upload require separate development phase.

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
amfi=0x80
```

- `amfi=0x80`: Disables AMFI enforcement to allow loading unsigned DriverKit extensions.

**Required DeviceProperties:**
Inject the following into your `DeviceProperties` to spoof the device as an Intel Ice Lake Gen11 iGPU (required for `AppleIntelICLLPGraphicsFramebuffer` binding):

> Before using this path, confirm it on your board:
>   gfxutil -f GFX0
> or:
>   ioreg -l | grep -i igpu

- **Path**: `PciRoot(0x0)/Pci(0x2,0x0)`
- **Properties**:
  - `device-id`: `<data>528A0000</data>`  <!-- Ice Lake GT2: 0x8A52 -->
  - `AAPL,ig-platform-id`: `<data>AABaig==</data>`  <!-- 0x5A08 -->

> [!IMPORTANT]
> Use EITHER OpenCore DeviceProperties OR NtelSpoofKext — NOT both.
> If you configured device-id above via OpenCore, do NOT load NtelSpoofKext.

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

| Symptom                                  | Likely Diagnosis               | The Fix                                                                                                          |
| :--------------------------------------- | :----------------------------- | :--------------------------------------------------------------------------------------------------------------- |
| **Immediate Kernel Panic on boot**       | AMFI or SIP is still active.   | Verify `boot-args` in NVRAM and ensure `amfi=0x80` is present.                                          |
| **Kext fails to load**                   | Binary not built or permissions. | Build the kext with KDK/Xcode, then run `chmod +x deploy-dev.sh`.                                       |

---

## 🧪 How It Works

Ntel uses identity spoofing to enable the Intel Alder Lake iGPU by leveraging Apple's native `AppleIntelICLLPGraphicsFramebuffer` driver. The architecture is:

1.  **PCIe Identity Spoofing**: `NtelSpoofKext` intercepts the `IOPCIDevice` probe to replace the device ID (`0x46A8`) with Intel Ice Lake Gen11 (`0x8A52`) and sets `AAPL,ig-platform-id` (`0x5A08`). This causes macOS to bind `AppleIntelICLLPGraphicsFramebuffer`, which natively supports the Xe architecture.
2.  **High-Speed IPC Ring**: `NtelSharedRing` provides a lock-free circular buffer for kernel-user communication.
3.  **Translation Engine** (Phase 2): `NtelTranslationEngine` provides a stub translation pipeline. Real AIR→Gen12 firmware translation requires significant additional development.
4.  **Firmware Injection** (Phase 2): `NtelFirmwareInjector` reads GuC/HuC blobs into kernel memory for future hardware upload. Actual GPU register programming not yet implemented.

For a deeper technical explanation of the memory model, ISA translation, and security protocols, please see the **Architecture Deep Dive**.

---

## 🛠️ Building From Source

We welcome contributions! To get started, you'll need a standard macOS development environment with the Kernel Development Kit (KDK).

### Firmware Sourcing

> [!WARNING]
> **Firmware upload to GPU registers is not implemented.** The `NtelFirmwareInjector` reads GuC/HuC blobs into kernel memory, but does not yet push them to hardware via GPU register writes.
> The linux-firmware blobs (`guc_xe_lp.bin` / `huc_xe_lp.bin`) are ELF-wrapped for Linux's request_firmware() API. Even if sourced, macOS has no equivalent loading pathway.
> For development, use `firmware/bin/guc_xe_lp.raw` and `huc_xe_lp.raw` — but these would only be read and checksummed, not uploaded to the GPU.

For detailed build instructions, debugging protocols, and contribution guidelines, please refer to the **Development & Engineering Guide**.

---

## ⚖️ License

it not right now This project is released under the MIT License. See the LICENSE file for details.
