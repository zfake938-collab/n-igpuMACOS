#!/bin/bash
# ==============================================================================
# NtelMacOS Deployment Orchestrator (Target: Intel i7/i5-1235U)
# ==============================================================================

echo "[NtelMacOS] Initializing Bare-Metal Deployment Sequence..."

# PRE-FLIGHT CHECK: Ensure the user has disabled AMFI/SIP in Recovery OS
# nvram boot-args="amfi_get_out_of_my_way=1 cs_allow_invalid=1 agdpmod=pikera"
if nvram boot-args | grep -q "amfi_get_out_of_my_way=1"; then
    echo "[PASS] AMFI bypass detected in NVRAM."
else
    echo "[WARNING] AMFI bypass not found in NVRAM. Dext loading may panic or halt."
    echo "          Please reboot into Recovery and run:"
    echo "          nvram boot-args=\"amfi_get_out_of_my_way=1 cs_allow_invalid=1 agdpmod=pikera\""
fi

# Step 1: Enable DriverKit Developer Mode
echo "[NtelMacOS] Toggling SystemExtensions Developer Mode..."
systemextensionsctl developer on

# Step 2: Fix Ownership and Permissions
echo "[NtelMacOS] Securing Kext and Dext permissions..."
sudo chown -R root:wheel ./NtelMacOS_Production/NtelMacOS.kext
sudo chmod -R 755 ./NtelMacOS_Production/NtelMacOS.kext

# Step 3: Load the Supervisor Kext (Phase I & IV)
echo "[NtelMacOS] Injecting Supervisor Kext (PCIe Spoofing & Cache Management)..."
sudo kextutil -v ./NtelMacOS_Production/NtelMacOS.kext

echo "[NtelMacOS] Deployment staged successfully."
echo "[NtelMacOS] To execute Smoke-Test verification matrix, run:"
echo "          ./sim_test --triangle"
