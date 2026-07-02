#include "NtelFirmwareInjector.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <kern/debug.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#endif

static NtelFirmwareBlob *get_blob_for_type(NtelFirmwareContext *ctx, NtelFirmwareType type) {
    switch (type) {
        case NTEL_FIRMWARE_TYPE_GUC: return &ctx->guc_blob;
        case NTEL_FIRMWARE_TYPE_HUC: return &ctx->huc_blob;
        default: return NULL;
    }
}

static uint32_t simple_checksum(const uint8_t *data, uint32_t size) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum = (sum << 5) + sum + data[i];
    }
    return sum;
}

#ifdef __APPLE__

static NtelFirmwareResult ntel_fw_read_vnode(const char *path, NtelFirmwareBlob *blob) {
    vnode_t vp = NULL;
    vfs_context_t ctx = vfs_context_current();
    int error;

    error = vnode_open(path, O_RDONLY, 0, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
    if (error != 0 || vp == NULL) {
        printf("[FW] vnode_open failed for %s (error=%d)\n", path, error);
        return NTEL_FW_ERR_VNODE_OPEN;
    }

    struct vnode_attr va;
    VATTR_INIT(&va);
    VATTR_WANTED(&va, va_data_size);
    error = vnode_getattr(vp, &va, ctx);
    if (error != 0) {
        vnode_close(vp, O_RDONLY, ctx);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    if (va.va_data_size == 0 || va.va_data_size > NTEL_FW_MAX_BLOB_SIZE) {
        vnode_close(vp, O_RDONLY, ctx);
        return NTEL_FW_ERR_TOO_LARGE;
    }

    blob->size = (uint32_t)va.va_data_size;
    blob->data = (uint8_t *)IOMallocContiguous(blob->size, 64, NULL);
    if (!blob->data) {
        blob->size = 0;
        blob->loaded = false;
        vnode_close(vp, O_RDONLY, ctx);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    int residual = 0;
    error = vn_rdwr(UIO_READ, vp, blob->data, blob->size, 0,
                    UIO_SYSSPACE, IO_NOCACHE,
                    vfs_context_ucred(ctx), &residual, NULL);

    vnode_close(vp, O_RDONLY, ctx);
    vp = NULL;

    if (error != 0 || residual != 0) {
        /* residual != 0 means fewer bytes were read than requested (short-read) */
        IOFreeContiguous(blob->data, blob->size);
        blob->data = NULL;
        blob->size = 0;
        blob->loaded = false;
        return NTEL_FW_ERR_VNODE_READ;
    }

    blob->loaded = true;
    return NTEL_FW_SUCCESS;
}

#else

static NtelFirmwareResult ntel_fw_read_file(const char *path, NtelFirmwareBlob *blob) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("[FW] Failed to open %s\n", path);
        return NTEL_FW_ERR_NOT_FOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    long fsize = ftell(fp);
    if (fsize <= 0) {
        fclose(fp);
        return NTEL_FW_ERR_LOAD_FAILED;  // empty or invalid file, not "too large"
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    if ((uint32_t)fsize > NTEL_FW_MAX_BLOB_SIZE) {
        fclose(fp);
        return NTEL_FW_ERR_TOO_LARGE;
    }

    blob->size = (uint32_t)fsize;
    blob->data = (uint8_t *)malloc(blob->size);
    if (!blob->data) {
        fclose(fp);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    size_t read_bytes = fread(blob->data, 1, blob->size, fp);
    fclose(fp);

    if (read_bytes != blob->size) {
        free(blob->data);
        blob->data = NULL;
        blob->size = 0;
        blob->loaded = false;
        return NTEL_FW_ERR_VNODE_READ;
    }

    blob->loaded = true;
    return NTEL_FW_SUCCESS;
}

#endif

NtelFirmwareResult ntel_fw_inject_all(NtelFirmwareContext *ctx) {
    if (!ctx) return NTEL_FW_ERR_LOAD_FAILED;

    NtelFirmwareResult guc_res = ntel_fw_load_blob(ctx, NTEL_FIRMWARE_TYPE_GUC,
                                                    "firmware/bin/guc_xe_lp.raw");
    if (guc_res != NTEL_FW_SUCCESS) {
        printf("[FW] GuC load failed: %d\n", guc_res);
        ntel_fw_cleanup(ctx);
        return guc_res;
    }

    NtelFirmwareResult huc_res = ntel_fw_load_blob(ctx, NTEL_FIRMWARE_TYPE_HUC,
                                                    "firmware/bin/huc_xe_lp.raw");
    if (huc_res != NTEL_FW_SUCCESS) {
        printf("[FW] HuC load failed: %d\n", huc_res);
        ntel_fw_cleanup(ctx);
        return huc_res;
    }

    /* Both blobs are confirmed loaded — only now mark the context flags.
       Setting these flags earlier would leave them inconsistent if the
       second blob load fails and cleanup is called. */
    ctx->guc_loaded = true;
    ctx->huc_loaded = true;

    printf("[FW] All firmware loaded successfully\n");
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_load_blob(NtelFirmwareContext *ctx, NtelFirmwareType type, const char *path) {
    if (!ctx || !path) return NTEL_FW_ERR_NOT_FOUND;

    NtelFirmwareBlob *blob = get_blob_for_type(ctx, type);
    if (!blob) return NTEL_FW_ERR_NOT_FOUND;

    if (blob->loaded) {
        printf("[FW] Blob already loaded for type %d\n", type);
        return NTEL_FW_SUCCESS;
    }

#ifdef __APPLE__
    NtelFirmwareResult res = ntel_fw_read_vnode(path, blob);
#else
    NtelFirmwareResult res = ntel_fw_read_file(path, blob);
#endif

    if (res != NTEL_FW_SUCCESS) return res;

    uint32_t checksum = simple_checksum(blob->data, blob->size);
    printf("[FW] Loaded %s (%u bytes, checksum=0x%08X)\n", path, blob->size, checksum);

    return NTEL_FW_SUCCESS;
}

bool ntel_fw_verify_status(NtelFirmwareContext *ctx) {
    if (!ctx) return false;
    return (ctx->guc_loaded && ctx->huc_loaded &&
            ctx->guc_blob.loaded && ctx->huc_blob.loaded);
}

void ntel_fw_cleanup(NtelFirmwareContext *ctx) {
    if (!ctx) return;

    if (ctx->guc_blob.data) {
#ifdef __APPLE__
        IOFreeContiguous(ctx->guc_blob.data, ctx->guc_blob.size);
#else
        free(ctx->guc_blob.data);
#endif
        ctx->guc_blob.data = NULL;
        ctx->guc_blob.size = 0;
        ctx->guc_blob.loaded = false;
    }

    if (ctx->huc_blob.data) {
#ifdef __APPLE__
        IOFreeContiguous(ctx->huc_blob.data, ctx->huc_blob.size);
#else
        free(ctx->huc_blob.data);
#endif
        ctx->huc_blob.data = NULL;
        ctx->huc_blob.size = 0;
        ctx->huc_blob.loaded = false;
    }

    if (ctx->mmio_base) {
#ifdef __APPLE__
        if (ctx->mmio_size > 0) {
            IOFreeContiguous(ctx->mmio_base, ctx->mmio_size);
        }
#else
        // Usermode simulation - no-op
#endif
        ctx->mmio_base = NULL;
        ctx->mmio_size = 0;
    }

    ctx->guc_loaded = false;
    ctx->huc_loaded = false;
}

#ifdef __APPLE__
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, IOPCIDevice *pci_device) {
    if (!ctx || !pci_device) return NTEL_FW_ERR_NOT_FOUND;
    
    // Map the GPU's MMIO registers
    // Intel VGA/MMIO base offset varies by device - typically 0x4000+
    ctx->mmio_size = 0x10000; // 64KB MMIO space
    ctx->mmio_base = IOMallocContiguous(ctx->mmio_size, 4096, NULL);
    if (!ctx->mmio_base) {
        printf("[FW] Failed to allocate MMIO mapping space\n");
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    
    // Map physical registers - in real implementation would use IOPCIDevice::mapMemory
    // This is a placeholder for the MMIO mapping logic
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx) {
    if (!ctx || !ctx->mmio_base || !ctx->guc_blob.loaded) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    
    uint8_t *mmio = (uint8_t *)ctx->mmio_base;
    uint8_t *guc_data = ctx->guc_blob.data;
    uint32_t guc_size = ctx->guc_blob.size;
    
    // Step 1: Disable GuC
    *(volatile uint32_t *)(mmio + INTEL_GUC_STATUS_REG_OFFSET) = 0x00000000;
    
    // Step 2: Write GuC blob to loader register in chunks
    uint32_t *load_reg = (uint32_t *)(mmio + INTEL_GUC_LOAD_REG_OFFSET);
    for (uint32_t i = 0; i < guc_size; i += 4) {
        *load_reg = *(uint32_t *)(guc_data + i);
    }
    
    // Step 3: Trigger upload and wait for completion
    *(volatile uint32_t *)(mmio + INTEL_Doorbell_REG_OFFSET) = 0x00000001;
    
    // Poll for completion with timeout
    for (uint32_t i = 0; i < INTEL_GUC_LOAD_COMPLETE_TIMEOUT; i++) {
        if (*(volatile uint32_t *)(mmio + INTEL_GUC_STATUS_REG_OFFSET) & 0x00000001) {
            printf("[FW] GuC upload completed\n");
            return NTEL_FW_SUCCESS;
        }
    }
    
    printf("[FW] GuC upload timeout\n");
    return NTEL_FW_ERR_HARDWARE_REJECT;
}

NtelFirmwareResult ntel_fw_upload_huc_to_hw(NtelFirmwareContext *ctx) {
    if (!ctx || !ctx->mmio_base || !ctx->huc_blob.loaded) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    
    uint8_t *mmio = (uint8_t *)ctx->mmio_base;
    uint8_t *huc_data = ctx->huc_blob.data;
    uint32_t huc_size = ctx->huc_blob.size;
    
    // Step 1: Disable HuC
    *(volatile uint32_t *)(mmio + INTEL_HUC_STATUS_REG_OFFSET) = 0x00000000;
    
    // Step 2: Write HuC blob to loader register in chunks
    uint32_t *load_reg = (uint32_t *)(mmio + INTEL_GUC_LOAD_REG_OFFSET + 0x1000);
    for (uint32_t i = 0; i < huc_size; i += 4) {
        *load_reg = *(uint32_t *)(huc_data + i);
    }
    
    // Step 3: Trigger upload and wait for completion
    *(volatile uint32_t *)(mmio + INTEL_Doorbell_REG_OFFSET) = 0x00000002;
    
    // Poll for completion with timeout
    for (uint32_t i = 0; i < INTEL_GUC_LOAD_COMPLETE_TIMEOUT; i++) {
        if (*(volatile uint32_t *)(mmio + INTEL_HUC_STATUS_REG_OFFSET) & 0x00000001) {
            printf("[FW] HuC upload completed\n");
            return NTEL_FW_SUCCESS;
        }
    }
    
    printf("[FW] HuC upload timeout\n");
    return NTEL_FW_ERR_HARDWARE_REJECT;
}
#else
// Usermode stubs for simulation
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, void *unused) {
    (void)unused;
    ctx->mmio_base = NULL;
    ctx->mmio_size = 0;
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx) {
    (void)ctx;
    printf("[FW] GuC MMIO upload (usermode stub - no hardware access)\n");
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_huc_to_hw(NtelFirmwareContext *ctx) {
    (void)ctx;
    printf("[FW] HuC MMIO upload (usermode stub - no hardware access)\n");
    return NTEL_FW_SUCCESS;
}
#endif
