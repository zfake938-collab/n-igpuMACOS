#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>

// Intel UHD 770 (Alder Lake) — 1235U iGPU device ID is 0x46A8
#define INTEL_UHD770_VENDOR_ID 0x8086
#define INTEL_UHD770_DEVICE_ID 0x46A8

// Spoofed AMD Radeon (Targeting a mobile Polaris-based chip for AMDRadeonX4000 compatibility)
// Example: AMD Radeon RX 550 / Vega Mobile target
#define SPOOFED_AMD_VENDOR_ID 0x1002
#define SPOOFED_AMD_DEVICE_ID 0x7340 

class NtelSpoofService : public IOService {
    OSDeclareDefaultStructors(NtelSpoofService)

public:
    virtual bool init(OSObject *provider) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(NtelSpoofService, IOService)

bool NtelSpoofService::init(OSObject *provider) {
    if (!IOService::init(provider)) return false;
    return true;
}

bool NtelSpoofService::start(IOService *provider) {
    IOPCIDevice *pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        return false;
    }

    // Verify we are actually looking at the Intel UHD 770
    if (pciDevice->getVendorID() == INTEL_UHD770_VENDOR_ID &&
        pciDevice->getDeviceID() == INTEL_UHD770_DEVICE_ID) {
        
        // Perform the Spoofing
        // Note: In a real Kext, we must manipulate the registry properties
        // so that subsequent IOKit matching uses the spoofed IDs.
        
        pciDevice->setProperty("vendor-id", (OSNumber *)OSNumber::withNumber(SPOOFED_AMD_VENDOR_ID, 32));
        pciDevice->setProperty("device-id", (OSNumber *)OSNumber::withNumber(SPOOFED_AMD_DEVICE_ID, 32));
        
        // Also spoof the class code to match AMD Graphics
        pciDevice->setProperty("class-code", (OSNumber *)OSNumber::withNumber(0x03000000, 32));

        IOLog("NtelSpoofKext: Successfully intercepted Intel 0x46A8 and spoofed to AMD 0x%x\n", SPOOFED_AMD_DEVICE_ID);
    }

    return IOService::start(provider);
}

void NtelSpoofService::stop(IOService *provider) {
    IOLog("NtelSpoofKext: Stopping service\n");
    IOService::stop(provider);
}
