#include "NtelFirmwareInjector.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <kern/debug.h>
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
    vnode_t vp;
    vfs_context_t ctx = vfs_context_current();
    int error;

    error = vnode_open(path, O_RDONLY, 0, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
    if (error != 0) {
        printf("[FW] vnode_open failed for %s (error=%d)\n", path, error);
        return NTEL_FW_ERR_VNODE_OPEN;
    }

    struct vnode_attr va;
    VATTR_INIT(&va);
    VATTR_WANTED(&va, va_data_size);
    error = vnode_getattr(vp, &va, ctx);
    if (error != 0 || va.va_data_size > NTEL_FW_MAX_BLOB_SIZE) {
        vnode_close(vp, O_RDONLY, ctx);
        return NTEL_FW_ERR_TOO_LARGE;
    }

    blob->size = (uint32_t)va.va_data_size;
    blob->data = (uint8_t *)IOMallocAligned(blob->size, 64);
    if (!blob->data) {
        vnode_close(vp, O_RDONLY, ctx);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    uio_t uio;
    uio_create(1, 0, UIO_SYSSPACE, UIO_READ, &uio);
    uio_addiov(uio, CAST_USER_ADDR_T(blob->data), blob->size);

    error = vn_rdwr(UIO_READ, vp, blob->data, blob->size, 0,
                    UIO_SYSSPACE, IO_NODELOCKED | IO_NOCACHE,
                    vfs_context_ucred(ctx), NULL, NULL);

    vnode_close(vp, O_RDONLY, ctx);

    if (error != 0) {
        IOFreeAligned(blob->data, blob->size);
        blob->data = NULL;
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

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || (uint32_t)fsize > NTEL_FW_MAX_BLOB_SIZE) {
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
        return guc_res;
    }
    ctx->guc_loaded = true;

    NtelFirmwareResult huc_res = ntel_fw_load_blob(ctx, NTEL_FIRMWARE_TYPE_HUC,
                                                    "firmware/bin/huc_xe_lp.raw");
    if (huc_res != NTEL_FW_SUCCESS) {
        printf("[FW] HuC load failed: %d\n", huc_res);
        return huc_res;
    }
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
        IOFreeAligned(ctx->guc_blob.data, ctx->guc_blob.size);
#else
        free(ctx->guc_blob.data);
#endif
        ctx->guc_blob.data = NULL;
        ctx->guc_blob.loaded = false;
    }

    if (ctx->huc_blob.data) {
#ifdef __APPLE__
        IOFreeAligned(ctx->huc_blob.data, ctx->huc_blob.size);
#else
        free(ctx->huc_blob.data);
#endif
        ctx->huc_blob.data = NULL;
        ctx->huc_blob.loaded = false;
    }

    ctx->guc_loaded = false;
    ctx->huc_loaded = false;
}
