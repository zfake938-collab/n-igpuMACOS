#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>

#define INTEL_UHD770_VENDOR_ID 0x8086
#define INTEL_UHD770_DEVICE_ID 0x46A8

// Intel Ice Lake (Gen11) iGPU — closest supported architecture to 12th Gen Iris Xe
// AAPL,ig-platform-id: 0x00005A08 (AABaig== base64)
// device-id: 0x8A52 (UooAAA== base64) — Ice Lake GT2
#define SPOOFED_IGPU_PLATFORM_ID 0x00005A08
#define SPOOFED_DEVICE_ID        0x8A52
#define INTEL_VENDOR_ID          0x8086

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

    if (pciDevice->getVendorID() != INTEL_UHD770_VENDOR_ID ||
        pciDevice->getDeviceID() != INTEL_UHD770_DEVICE_ID) {
        IOLog("NtelSpoofKext: Not target device (vendor=0x%x, device=0x%x)\n",
              pciDevice->getVendorID(), pciDevice->getDeviceID());
        return false;
    }

    // Spoof to Intel Ice Lake Gen11 iGPU for AppleIntelICLLPGraphicsFramebuffer compatibility
    pciDevice->setProperty("AAPL,ig-platform-id",
                           OSData::withBytes(&SPOOFED_IGPU_PLATFORM_ID, sizeof(SPOOFED_IGPU_PLATFORM_ID)));
    pciDevice->setProperty("device-id",
                           OSData::withBytes(&SPOOFED_DEVICE_ID, sizeof(SPOOFED_DEVICE_ID)));
    pciDevice->setProperty("vendor-id",
                           OSData::withBytes(&INTEL_VENDOR_ID, sizeof(INTEL_VENDOR_ID)));

    IOLog("NtelSpoofKext: Spoofed Intel UHD 770 (0x46A8) -> Ice Lake Gen11 (0x%04X, platform=0x%08X)\n",
          SPOOFED_DEVICE_ID, SPOOFED_IGPU_PLATFORM_ID);

    return IOService::start(provider);
}

void NtelSpoofService::stop(IOService *provider) {
    IOLog("NtelSpoofKext: Stopping service\n");
    IOService::stop(provider);
}
