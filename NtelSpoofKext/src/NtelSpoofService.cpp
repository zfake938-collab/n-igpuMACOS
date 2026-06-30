#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSData.h>

static const uint32_t kIntelUhd770VendorId = 0x8086;
static const uint32_t kIntelUhd770DeviceId = 0x46A8;

// Intel Ice Lake (Gen11) iGPU - closest supported architecture to 12th Gen Iris Xe
// AAPL,ig-platform-id: 0x00005A08 (AABaig== base64)
// device-id: 0x8A52 (UooAAA== base64) - Ice Lake GT2
static const uint32_t kSpoofedIgpuPlatformId = 0x00005A08;
static const uint32_t kSpoofedDeviceId = 0x00008A52;
static const uint32_t kIntelVendorId = 0x00008086;

static bool setUInt32DataProperty(IOPCIDevice *pciDevice, const char *key, const uint32_t *value) {
    OSData *data = OSData::withBytes(value, sizeof(*value));
    if (!data) {
        return false;
    }

    bool ok = pciDevice->setProperty(key, data);
    data->release();
    return ok;
}

class NtelSpoofService : public IOService {
    OSDeclareDefaultStructors(NtelSpoofService)

public:
    virtual bool init(OSDictionary *dictionary = NULL) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(NtelSpoofService, IOService)

bool NtelSpoofService::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) return false;
    return true;
}

bool NtelSpoofService::start(IOService *provider) {
    IOPCIDevice *pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        return false;
    }

    if (pciDevice->getVendorID() != kIntelUhd770VendorId ||
        pciDevice->getDeviceID() != kIntelUhd770DeviceId) {
        IOLog("NtelSpoofKext: Not target device (vendor=0x%x, device=0x%x)\n",
              pciDevice->getVendorID(), pciDevice->getDeviceID());
        return false;
    }

    // Spoof to Intel Ice Lake Gen11 iGPU for AppleIntelICLLPGraphicsFramebuffer compatibility
    if (!setUInt32DataProperty(pciDevice, "AAPL,ig-platform-id", &kSpoofedIgpuPlatformId) ||
        !setUInt32DataProperty(pciDevice, "device-id", &kSpoofedDeviceId) ||
        !setUInt32DataProperty(pciDevice, "vendor-id", &kIntelVendorId)) {
        IOLog("NtelSpoofKext: Failed to allocate spoof registry properties\n");
        return false;
    }

    IOLog("NtelSpoofKext: Spoofed Intel UHD 770 (0x46A8) -> Ice Lake Gen11 (0x%04X, platform=0x%08X)\n",
          kSpoofedDeviceId, kSpoofedIgpuPlatformId);

    return IOService::start(provider);
}

void NtelSpoofService::stop(IOService *provider) {
    IOLog("NtelSpoofKext: Stopping service\n");
    IOService::stop(provider);
}
