#ifndef NTEL_FIRMWARE_INJECTOR_H
#define NTEL_FIRMWARE_INJECTOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief GuC/HuC Firmware Type identifier.
 */
typedef enum {
    NTEL_FIRMWARE_TYPE_GUC,
    NTEL_FIRMWARE_TYPE_HUC,
    NTEL_FIRMWARE_TYPE_UNKNOWN
} NtelFirmwareType;

/**
 * @brief Firmware Injection Result codes.
 */
typedef enum {
    NTEL_FW_SUCCESS = 0,
    NTEL_FW_ERR_NOT_FOUND,
    NTEL_FW_ERR_LOAD_FAILED,
    NTEL_FW_ERR_INVALID_SIGNATURE,
    NTEL_FW_ERR_HARDWARE_REJECT
} NtelFirmwareResult;

/**
 * @brief Firmware Injection Context.
 * Manages the loading of Intel GuC/HuC blobs into the GPU microcontrollers.
 */
typedef struct {
    uint32_t device_id;
    uint32_t firmware_version;
    bool guc_loaded;
    bool huc_loaded;
} NtelFirmwareContext;

// Core API
NtelFirmwareResult ntel_fw_inject_all(NtelFirmwareContext *ctx);
NtelFirmwareResult ntel_fw_load_blob(NtelFirmwareContext *ctx, NtelFirmwareType type, const char *path);
bool ntel_fw_verify_status(NtelFirmwareContext *ctx);

#endif // NTEL_FIRMWARE_INJECTOR_H
