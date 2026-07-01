#!/bin/bash
# ==============================================================================
# NtelMacOS Deployment Orchestrator (Target: Intel i7/i5-1235U)
# ==============================================================================

echo "[NtelMacOS] Initializing Bare-Metal Deployment Sequence..."

# PRE-FLIGHT CHECK: Ensure the user has disabled AMFI in Recovery OS
if nvram boot-args | grep -q "amfi=0x80"; then
    echo "[PASS] AMFI bypass detected in NVRAM."
else
    echo "[WARNING] AMFI bypass not found in NVRAM. Kext loading may fail."
    echo "          Please reboot into Recovery and run:"
    echo "          nvram boot-args=\"amfi=0x80\""
fi

# Step 2: Fix Ownership and Permissions
echo "[NtelMacOS] Securing Kext permissions..."
sudo chown -R root:wheel ./NtelMacOS.kext
sudo chmod -R 755 ./NtelMacOS.kext

# Step 3: Load the Supervisor Kext
echo "[NtelMacOS] Injecting Supervisor Kext (PCIe Spoofing)..."
if [ -f "./NtelMacOS.kext/Contents/MacOS/NtelMacOS" ]; then
    # kextutil is deprecated since macOS 13 Ventura; use kmutil load instead.
    # We keep kextutil as a fallback for macOS 12 (Monterey).
    OS_MAJOR=$(sw_vers -productVersion | cut -d. -f1)
    if [ "$OS_MAJOR" -ge 13 ] 2>/dev/null; then
        echo "[NtelMacOS] Using kmutil (macOS $OS_MAJOR)..."
        sudo kmutil load -u ./NtelMacOS.kext
    else
        echo "[NtelMacOS] Using kextutil (macOS $OS_MAJOR / legacy)..."
        sudo kextutil -v ./NtelMacOS.kext
    fi
else
    echo "[ERROR] Kext binary not found. Build the kext first with Xcode/KDK."
    exit 1
fi

echo "[NtelMacOS] Deployment staged successfully."
echo "[NtelMacOS] To verify spoofing worked, run on target:"
echo "          ioreg -lw0 -p IOService | grep -i \"AAPL,ig-platform-id\""
echo ""
echo "[NtelMacOS] Note: The kext/dext binaries must be built first before loading."
