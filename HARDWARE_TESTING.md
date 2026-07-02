# Ntel Hardware Testing Guide

**Target Hardware**: Intel Core i5/i7-1235U (Alder Lake-U, Iris Xe Graphics)  
**Current Phase**: Phase 1 (PCIe Identity Spoofing) - Tested and Ready  
**Phase 2 Status**: Translation Engine & Firmware Upload - Not Implemented

---

## 🔴 CRITICAL: Before You Begin

### Safety Precautions (Non-Negotiable)

1. **Create Recovery Media**
   ```bash
   # Create bootable macOS recovery USB
   # Use createinstallmedia or balenaEtcher with macOS installer
   ```

2. **Document Current System**
   ```bash
   # Record your exact configuration
   system_profiler SPHardwareDataType | grep "Model\|Chip\|Processor"
   sw_vers
   ioreg -l | grep -i "GFX0\|IGPU"
   gfxutil -f GFX0  # if available
   ```

3. **Have Rollback Plan Ready**
   - Know how to boot into Recovery OS (Cmd+R)
   - Know how to disable kext loading in OpenCore picker
   - Test recovery USB before proceeding

---

## 📋 Pre-Deployment Checklist

Before installing on hardware, ensure all items are complete:

| Task | Status | Notes |
|------|--------|-------|
| Recovery media created and tested | ☐ | Critical - no recovery = high risk |
| System documented (model, macOS version) | ☐ | Required for debugging |
| OpenCore with Lilu/WhateverGreen installed | ☐ | Required dependencies |
| `amfi=0x80` set in NVRAM | ☐ | Required for kext loading |
| Kext binary built successfully | ☐ | Check NtelMacOS.kext/Contents/MacOS/ |

---

## 🔧 Deployment Procedure

### Step 1: Configure OpenCore (If Not Using NtelSpoofKext)

If you prefer OpenCore spoofing instead of the kext, inject these properties:

| Path | Property | Value |
|------|----------|-------|
| `PciRoot(0x0)/Pci(0x2,0x0)` | `device-id` | `<528A0000>` |
| `PciRoot(0x0)/Pci(0x2,0x0)` | `AAPL,ig-platform-id` | `<CFoAAA==>` |

> **Warning**: Use EITHER OpenCore DeviceProperties OR NtelSpoofKext - NOT both.

### Step 2: Prepare Kext

```bash
cd NtelMacOS_Production
# Verify kext binary exists
ls -la NtelMacOS.kext/Contents/MacOS/NtelMacOS

# Set permissions (optional - deploy-dev.sh does this)
sudo chown -R root:wheel NtelMacOS.kext
sudo chmod -R 755 NtelMacOS.kext
```

### Step 3: Deploy

```bash
# Run deployment orchestrator
sudo ./deploy-dev.sh

# For macOS 13+, uses kmutil:
# sudo kmutil load -u ./NtelMacOS.kext

# For macOS 12 and earlier, uses kextutil:
# sudo kextutil -v ./NtelMacOS.kext
```

### Step 4: Reboot

Full reboot required for IOPCIDevice probe interception.

---

## ✅ Verification Commands (Post-Boot)

### Confirm Spoofing Worked

```bash
# Check spoofed device ID (should show 0x8A52)
ioreg -lw0 -p IOService -r -c IOPCIDevice | grep -A5 -B5 "0x8a52\|8A52"

# Check platform ID injection
ioreg -lw0 -p IOService | grep -i "AAPL,ig-platform-id"

# Check kext loaded
kextstat | grep -i ntel

# Check framebuffer loaded (may fail - this is expected)
ioreg -lw0 -p IOService | grep -i "AppleIntelICLLPGraphicsFramebuffer"
```

### Enhanced Debug (Optional)

Add boot argument `ntel_debug=4` to NVRAM for verbose logging:

```bash
# In Recovery Terminal:
nvram boot-args="amfi=0x80 ntel_debug=4"
```

Then check logs:
```bash
log show --predicate 'subsystem == "com.ntel"' --last 1h
```

---

## 📊 Expected Results

### Phase 1: What WILL Work

| Feature | Status | Expected Output |
|---------|--------|-----------------|
| Device spoofing | ✅ | `device-id` shows `0x8A52` in ioreg |
| Platform ID | ✅ | `AAPL,ig-platform-id` shows `0x5A08` |
| Kext loads | ✅ | `kextstat` shows NtelMacOS |
| Boot success | ✅ | System boots without panic |

### Phase 2: What WON'T Work (Not Implemented)

| Feature | Status | Notes |
|---------|--------|-------|
| GPU acceleration | ❌ | No translation engine |
| Metal framework | ❌ | Shaders not translated |
| Hardware rendering | ❌ | Firmware upload stub only |

---

## ⚠️ Troubleshooting

### Immediate Kernel Panic

**Diagnosis**: AMFI/SIP still active

**Fix**:
```bash
# Boot to Recovery
# Terminal:
nvram boot-args="amfi=0x80"
```

### Kext Won't Load

**Diagnosis**: Binary missing or wrong permissions

**Fix**:
```bash
# Check binary exists
ls NtelMacOS.kext/Contents/MacOS/

# Rebuild if needed
# Then redeploy:
sudo ./deploy-dev.sh
```

### Device Not Spoofed

**Diagnosis**: Wrong device ID or kext not matched

**Fix**:
```bash
# Verify your device matches 0x46A8 (1235U)
lspci -nn | grep -i vga
# or on macOS:
ioreg -l | grep -i "8086.*46a8"
```

### Framebuffer Not Binding

**Diagnosis**: Expected - Phase 2 not complete

**Note**: Even if spoofing works, `AppleIntelICLLPGraphicsFramebuffer` may not fully bind without translation engine.

---

## 🛑 Emergency Procedures

### Disable Kext at Boot

1. In OpenCore picker, press Spacebar
2. Select "Disable Kext Injection" or similar
3. Boot normally

### Remove Kext Manually

Boot from Recovery USB:
```bash
# Mount system volume
diskutil list
diskutil mount diskXs2  # Adjust X for your system

# Remove kext
rm -rf /Volumes/Macintosh\ HD/System/Library/Extensions/NtelMacOS.kext

# Rebuild cache
kmutil install --reset-assets --force
```

---

## 📝 Hardware Validation Checklist

After deployment, verify these items:

- [ ] System boots successfully (no kernel panic)
- [ ] `ioreg` shows spoofed device ID `0x8A52`
- [ ] `kextstat` shows NtelMacOS loaded
- [ ] No GPU-related errors in console logs
- [ ] Display works (may fall back to VESA if framebuffer fails)

---

## 📞 Reporting Issues

When reporting test results, include:

1. **Exact hardware model**: `system_profiler SPHardwareDataType`
2. **macOS version**: `sw_vers -productVersion`
3. **OpenCore version**: OpenCore config.plist or picker
4. **Full ioreg output**: `ioreg -lw0 -p IOService > ioreg.txt`
5. **Console logs**: `log show --predicate 'subsystem == "com.ntel"' --last 1h`
6. **Screenshot of panic** (if occurred): Photo preferred

---

## 🔄 Rollback to Stock

If issues occur:

1. Boot into Recovery OS
2. Remove kext from system volume
3. Clear NVRAM: `nvram -c` (if boot-args were the issue)
4. Rebuild kernel cache (if kext was installed)
5. Normal reboot

---

**Last Updated**: Phase 1 Complete - Ready for Hardware Testing  
**Next Milestone**: Phase 2 - Translation Engine Implementation