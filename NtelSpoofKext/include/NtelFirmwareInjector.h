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
} NtelFirmwareContext;

NtelFirmwareResult ntel_fw_inject_all(NtelFirmwareContext *ctx);
NtelFirmwareResult ntel_fw_load_blob(NtelFirmwareContext *ctx, NtelFirmwareType type, const char *path);
bool ntel_fw_verify_status(NtelFirmwareContext *ctx);
void ntel_fw_cleanup(NtelFirmwareContext *ctx);

#endif // NTEL_FIRMWARE_INJECTOR_H
