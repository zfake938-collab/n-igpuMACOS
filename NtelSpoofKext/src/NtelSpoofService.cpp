#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSData.h>
#include <mach/mach_time.h>

// Canonical Intel PCI vendor ID — used for both matching and registry property
static const uint32_t kIntelVendorId       = 0x00008086;
// Intel UHD 770 (Alder Lake iGPU, 12th Gen) device ID — the target we spoof FROM
static const uint32_t kIntelUhd770DeviceId = 0x46A8;

// Intel Ice Lake (Gen11) iGPU - closest supported architecture to 12th Gen Iris Xe
// AAPL,ig-platform-id: 0x00005A08 (AABaig== base64)
// device-id: 0x8A52 (UooAAA== base64) - Ice Lake GT2
static const uint32_t kSpoofedIgpuPlatformId = 0x00005A08;
static const uint32_t kSpoofedDeviceId        = 0x00008A52;

// Device validation for 1235U (Intel Core i5/i7-1235U)
static bool validate_1235U_hardware(IOPCIDevice *pciDevice) {
    uint16_t vendorId = pciDevice->getVendorID();
    uint16_t deviceId = pciDevice->getDeviceID();
    
    // Verify vendor is Intel
    if (vendorId != 0x8086) {
        IOLog("NtelSpoofKext: ERROR - Non-Intel device detected (vendor=0x%04X)\n", vendorId);
        return false;
    }
    
    // Verify device ID is 1235U iGPU (0x46A8)
    if (deviceId != kIntelUhd770DeviceId) {
        IOLog("NtelSpoofKext: WARNING - Unexpected device ID 0x%04X (expected 0x46A8 for 1235U)\n", deviceId);
        IOLog("NtelSpoofKext: This device may not be Intel Core i5/i7-1235U\n");
        // Continue anyway - allow testing on similar hardware
    }
    
    IOLog("NtelSpoofKext: Validated Intel GPU device 0x%04X\n", deviceId);
    return true;
}

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
    uint64_t start_time = mach_absolute_time();
    
    IOPCIDevice *pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        IOLog("NtelSpoofKext: ERROR - Provider is not IOPCIDevice\n");
        return false;
    }
    
    // Validate hardware before spoofing
    if (!validate_1235U_hardware(pciDevice)) {
        IOLog("NtelSpoofKext: Hardware validation failed - aborting\n");
        return false;
    }
    
    uint16_t vendorId = pciDevice->getVendorID();
    uint16_t deviceId = pciDevice->getDeviceID();
    
    // Spoof to Intel Ice Lake Gen11 iGPU for AppleIntelICLLPGraphicsFramebuffer compatibility
    if (!setUInt32DataProperty(pciDevice, "AAPL,ig-platform-id", &kSpoofedIgpuPlatformId) ||
        !setUInt32DataProperty(pciDevice, "device-id", &kSpoofedDeviceId) ||
        !setUInt32DataProperty(pciDevice, "vendor-id", &kIntelVendorId)) {
        IOLog("NtelSpoofKext: ERROR - Failed to set spoof registry properties\n");
        return false;
    }
    
    uint64_t end_time = mach_absolute_time();
    IOLog("NtelSpoofKext: Spoofed Intel UHD 770 (0x%04X) -> Ice Lake Gen11 (0x%04X, platform=0x%08X)\n",
          deviceId, kSpoofedDeviceId, kSpoofedIgpuPlatformId);
    IOLog("NtelSpoofKext: Spoofing completed in %llu ticks\n", end_time - start_time);

    return IOService::start(provider);
}

void NtelSpoofService::stop(IOService *provider) {
    IOLog("NtelSpoofKext: Stopping service\n");
    IOService::stop(provider);
}