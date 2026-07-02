#ifndef NTEL_FIRMWARE_INJECTOR_H
#define NTEL_FIRMWARE_INJECTOR_H

#include <stdint.h>
#include <stdbool.h>

#define NTEL_FW_MAX_BLOB_SIZE (4 * 1024 * 1024) // 4MB max firmware blob

typedef enum {
    NTEL_FIRMWARE_TYPE_GUC,
    NTEL_FIRMWARE_TYPE_HUC,
    NTEL_FIRMWARE_TYPE_UNKNOWN
} NtelFirmwareType;

typedef enum {
    NTEL_FW_SUCCESS = 0,
    NTEL_FW_ERR_NOT_FOUND,
    NTEL_FW_ERR_LOAD_FAILED,
    NTEL_FW_ERR_INVALID_SIGNATURE,
    NTEL_FW_ERR_HARDWARE_REJECT,
    NTEL_FW_ERR_VNODE_OPEN,
    NTEL_FW_ERR_VNODE_READ,
    NTEL_FW_ERR_TOO_LARGE
} NtelFirmwareResult;

typedef struct {
    uint8_t *data;
    uint32_t size;
    bool loaded;
} NtelFirmwareBlob;

typedef struct {
    uint32_t device_id;
    uint32_t firmware_version;
    bool guc_loaded;
    bool huc_loaded;
    NtelFirmwareBlob guc_blob;
    NtelFirmwareBlob huc_blob;
    // MMIO registers for hardware upload
    void *mmio_base;
    uint32_t mmio_size;
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
    void *mmio_map;   // IOMemoryMap * in kernel build
#endif
} NtelFirmwareContext;

// MMIO register offsets for Intel GuC/HuC (common values for Xe-LP)
#define INTEL_GUC_STATUS_REG_OFFSET     0x4000
#define INTEL_HUC_STATUS_REG_OFFSET     0x4400
#define INTEL_GUC_LOAD_REG_OFFSET       0x4800
#define INTEL_Doorbell_REG_OFFSET       0x4C00
#define INTEL_GUC_LOAD_COMPLETE_TIMEOUT 1000000

NtelFirmwareResult ntel_fw_inject_all(NtelFirmwareContext *ctx);
NtelFirmwareResult ntel_fw_load_blob(NtelFirmwareContext *ctx, NtelFirmwareType type, const char *path);
bool ntel_fw_verify_status(NtelFirmwareContext *ctx);
void ntel_fw_cleanup(NtelFirmwareContext *ctx);

// Phase 2: Hardware MMIO upload functions (macOS kernel build only)
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
#include <IOKit/pci/IOPCIDevice.h>
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, IOPCIDevice *pci_device);
#else
// Usermode stubs - declarations only
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, void *unused);
#endif
NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx);
NtelFirmwareResult ntel_fw_upload_huc_to_hw(NtelFirmwareContext *ctx);

#endif // NTEL_FIRMWARE_INJECTOR_H
