#include "NtelFirmwareInjector.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <kern/debug.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryMap.h>
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

#define NTEL_ELF_IDENT0 0x7F
#define NTEL_ELF_IDENT1 'E'
#define NTEL_ELF_IDENT2 'L'
#define NTEL_ELF_IDENT3 'F'
#define NTEL_ELF_CLASS_32 1
#define NTEL_ELF_CLASS_64 2
#define NTEL_ELF_DATA_LSB 1
#define NTEL_ELF_PT_LOAD 1

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

static bool ntel_fw_is_elf(const uint8_t *data, uint32_t size) {
    return size >= 4 &&
           data[0] == NTEL_ELF_IDENT0 &&
           data[1] == NTEL_ELF_IDENT1 &&
           data[2] == NTEL_ELF_IDENT2 &&
           data[3] == NTEL_ELF_IDENT3;
}

static NtelFirmwareResult ntel_fw_extract_elf_load_segments(const uint8_t *data,
                                                            uint32_t size,
                                                            NtelFirmwareBlob *blob) {
    if (!data || size < 16 || !blob) {
        return NTEL_FW_ERR_INVALID_SIGNATURE;
    }

    uint8_t klass = data[4];
    uint8_t encoding = data[5];
    if (encoding != NTEL_ELF_DATA_LSB) {
        return NTEL_FW_ERR_INVALID_SIGNATURE;
    }

    uint64_t total_bytes = 0;
    if (klass == NTEL_ELF_CLASS_64) {
        if (size < sizeof(Elf64_Ehdr)) return NTEL_FW_ERR_INVALID_SIGNATURE;
        Elf64_Ehdr ehdr;
        memcpy(&ehdr, data, sizeof(ehdr));

        uint64_t ph_table_end = ehdr.e_phoff + ((uint64_t)ehdr.e_phnum * ehdr.e_phentsize);
        if (ph_table_end > size || ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
            return NTEL_FW_ERR_INVALID_SIGNATURE;
        }

        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            const uint8_t *ph_ptr = data + ehdr.e_phoff + ((uint64_t)i * ehdr.e_phentsize);
            Elf64_Phdr phdr;
            memcpy(&phdr, ph_ptr, sizeof(phdr));
            if (phdr.p_type != NTEL_ELF_PT_LOAD || phdr.p_filesz == 0) continue;
            if (phdr.p_offset + phdr.p_filesz > size) return NTEL_FW_ERR_INVALID_SIGNATURE;
            total_bytes += phdr.p_filesz;
            if (total_bytes > NTEL_FW_MAX_BLOB_SIZE) return NTEL_FW_ERR_TOO_LARGE;
        }

        if (total_bytes == 0) return NTEL_FW_ERR_INVALID_SIGNATURE;

        uint8_t *raw = (uint8_t *)malloc((size_t)total_bytes);
        if (!raw) return NTEL_FW_ERR_LOAD_FAILED;

        uint64_t write_at = 0;
        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            const uint8_t *ph_ptr = data + ehdr.e_phoff + ((uint64_t)i * ehdr.e_phentsize);
            Elf64_Phdr phdr;
            memcpy(&phdr, ph_ptr, sizeof(phdr));
            if (phdr.p_type != NTEL_ELF_PT_LOAD || phdr.p_filesz == 0) continue;
            memcpy(raw + write_at, data + phdr.p_offset, phdr.p_filesz);
            write_at += phdr.p_filesz;
        }

        blob->data = raw;
        blob->size = (uint32_t)total_bytes;
        blob->loaded = true;
        return NTEL_FW_SUCCESS;
    }

    if (klass == NTEL_ELF_CLASS_32) {
        if (size < sizeof(Elf32_Ehdr)) return NTEL_FW_ERR_INVALID_SIGNATURE;
        Elf32_Ehdr ehdr;
        memcpy(&ehdr, data, sizeof(ehdr));

        uint32_t ph_table_end = ehdr.e_phoff + ((uint32_t)ehdr.e_phnum * ehdr.e_phentsize);
        if (ph_table_end > size || ehdr.e_phentsize != sizeof(Elf32_Phdr)) {
            return NTEL_FW_ERR_INVALID_SIGNATURE;
        }

        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            const uint8_t *ph_ptr = data + ehdr.e_phoff + ((uint32_t)i * ehdr.e_phentsize);
            Elf32_Phdr phdr;
            memcpy(&phdr, ph_ptr, sizeof(phdr));
            if (phdr.p_type != NTEL_ELF_PT_LOAD || phdr.p_filesz == 0) continue;
            if (phdr.p_offset + phdr.p_filesz > size) return NTEL_FW_ERR_INVALID_SIGNATURE;
            total_bytes += phdr.p_filesz;
            if (total_bytes > NTEL_FW_MAX_BLOB_SIZE) return NTEL_FW_ERR_TOO_LARGE;
        }

        if (total_bytes == 0) return NTEL_FW_ERR_INVALID_SIGNATURE;

        uint8_t *raw = (uint8_t *)malloc((size_t)total_bytes);
        if (!raw) return NTEL_FW_ERR_LOAD_FAILED;

        uint32_t write_at = 0;
        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            const uint8_t *ph_ptr = data + ehdr.e_phoff + ((uint32_t)i * ehdr.e_phentsize);
            Elf32_Phdr phdr;
            memcpy(&phdr, ph_ptr, sizeof(phdr));
            if (phdr.p_type != NTEL_ELF_PT_LOAD || phdr.p_filesz == 0) continue;
            memcpy(raw + write_at, data + phdr.p_offset, phdr.p_filesz);
            write_at += phdr.p_filesz;
        }

        blob->data = raw;
        blob->size = (uint32_t)total_bytes;
        blob->loaded = true;
        return NTEL_FW_SUCCESS;
    }

    return NTEL_FW_ERR_INVALID_SIGNATURE;
}

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
#include <IOKit/pci/IOPCIDevice.h>

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

    uint32_t file_size = (uint32_t)fsize;
    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        fclose(fp);
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    size_t read_bytes = fread(file_data, 1, file_size, fp);
    fclose(fp);

    if (read_bytes != file_size) {
        free(file_data);
        return NTEL_FW_ERR_VNODE_READ;
    }

    if (ntel_fw_is_elf(file_data, file_size)) {
        NtelFirmwareResult elf_res = ntel_fw_extract_elf_load_segments(file_data, file_size, blob);
        free(file_data);
        if (elf_res == NTEL_FW_SUCCESS) {
            printf("[FW] ELF firmware image detected and raw segments extracted (%u bytes)\n", blob->size);
            return NTEL_FW_SUCCESS;
        }
        printf("[FW] ELF firmware image detected but extraction failed (%d)\n", elf_res);
        return elf_res;
    }

    blob->size = file_size;
    blob->data = file_data;
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

    /* Phase 2: In usermode simulation, map a fake MMIO window and perform
       the firmware upload sequence into the emulated MMIO space. */
    if (ntel_fw_map_mmio(ctx, NULL) == NTEL_FW_SUCCESS) {
        NtelFirmwareResult guc_upload = ntel_fw_upload_guc_to_hw(ctx);
        NtelFirmwareResult huc_upload = ntel_fw_upload_huc_to_hw(ctx);
        if (guc_upload != NTEL_FW_SUCCESS || huc_upload != NTEL_FW_SUCCESS) {
            printf("[FW] Firmware upload failed: GuC=%d HuC=%d\n", guc_upload, huc_upload);
            ntel_fw_cleanup(ctx);
            return NTEL_FW_ERR_HARDWARE_REJECT;
        }
    }

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

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
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
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
        IOFreeContiguous(ctx->guc_blob.data, ctx->guc_blob.size);
#else
        free(ctx->guc_blob.data);
#endif
        ctx->guc_blob.data = NULL;
        ctx->guc_blob.size = 0;
        ctx->guc_blob.loaded = false;
    }

    if (ctx->huc_blob.data) {
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
        IOFreeContiguous(ctx->huc_blob.data, ctx->huc_blob.size);
#else
        free(ctx->huc_blob.data);
#endif
        ctx->huc_blob.data = NULL;
        ctx->huc_blob.size = 0;
        ctx->huc_blob.loaded = false;
    }

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
    if (ctx->mmio_map) {
        ((IOMemoryMap *)ctx->mmio_map)->release();
        ctx->mmio_map = NULL;
    }
#endif

    if (ctx->mmio_base) {
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
        // mmio_base is managed by mmio_map and should not be freed directly.
#else
        free(ctx->mmio_base);
#endif
        ctx->mmio_base = NULL;
        ctx->mmio_size = 0;
    }

    ctx->guc_loaded = false;
    ctx->huc_loaded = false;
}

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, IOPCIDevice *pci_device) {
    if (!ctx || !pci_device) return NTEL_FW_ERR_NOT_FOUND;
    if (ctx->mmio_map) return NTEL_FW_SUCCESS;

    const UInt8 bar_register = 0x10; // PCI BAR0
    IOMemoryMap *map = pci_device->mapDeviceMemoryWithRegister(bar_register, 0);
    if (!map) {
        printf("[FW] Failed to map PCI device MMIO BAR0\n");
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    ctx->mmio_map = map;
    ctx->mmio_base = (void *)(uintptr_t)map->GetAddress();
    ctx->mmio_size = (uint32_t)map->GetLength();
    if (ctx->mmio_size == 0 || ctx->mmio_base == NULL) {
        printf("[FW] MMIO mapping returned invalid address/size\n");
        map->release();
        ctx->mmio_map = NULL;
        ctx->mmio_base = NULL;
        ctx->mmio_size = 0;
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    printf("[FW] Mapped MMIO BAR0 @ %p len=%u\n", ctx->mmio_base, ctx->mmio_size);
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx) {
    if (!ctx || !ctx->mmio_base || !ctx->guc_blob.loaded) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    
    if (ntel_fw_check_mmio_space(ctx, INTEL_GUC_STATUS_REG_OFFSET, 4) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, INTEL_GUC_LOAD_REG_OFFSET, ctx->guc_blob.size) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, INTEL_Doorbell_REG_OFFSET, 4) != NTEL_FW_SUCCESS) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    uint8_t *mmio = (uint8_t *)ctx->mmio_base;
    uint8_t *guc_data = ctx->guc_blob.data;
    uint32_t guc_size = ctx->guc_blob.size;
    
    // Step 1: Disable GuC
    *(volatile uint32_t *)(mmio + INTEL_GUC_STATUS_REG_OFFSET) = 0x00000000;
    
    // Step 2: Write GuC blob to loader register in chunks
    uint32_t *load_reg = (uint32_t *)(mmio + INTEL_GUC_LOAD_REG_OFFSET);
    uint32_t full_words = guc_size / 4;
    uint32_t tail_bytes = guc_size % 4;
    for (uint32_t i = 0; i < full_words; i++) {
        load_reg[i] = *(uint32_t *)(guc_data + i * 4);
    }
    if (tail_bytes) {
        uint8_t *tail_ptr = (uint8_t *)(load_reg + full_words);
        memcpy(tail_ptr, guc_data + full_words * 4, tail_bytes);
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
    
    if (ntel_fw_check_mmio_space(ctx, INTEL_HUC_STATUS_REG_OFFSET, 4) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, INTEL_GUC_LOAD_REG_OFFSET + 0x1000, ctx->huc_blob.size) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, INTEL_Doorbell_REG_OFFSET, 4) != NTEL_FW_SUCCESS) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    uint8_t *mmio = (uint8_t *)ctx->mmio_base;
    uint8_t *huc_data = ctx->huc_blob.data;
    uint32_t huc_size = ctx->huc_blob.size;
    
    // Step 1: Disable HuC
    *(volatile uint32_t *)(mmio + INTEL_HUC_STATUS_REG_OFFSET) = 0x00000000;
    
    // Step 2: Write HuC blob to loader register in chunks
    uint32_t *load_reg = (uint32_t *)(mmio + INTEL_GUC_LOAD_REG_OFFSET + 0x1000);
    uint32_t full_words = huc_size / 4;
    uint32_t tail_bytes = huc_size % 4;
    for (uint32_t i = 0; i < full_words; i++) {
        load_reg[i] = *(uint32_t *)(huc_data + i * 4);
    }
    if (tail_bytes) {
        uint8_t *tail_ptr = (uint8_t *)(load_reg + full_words);
        memcpy(tail_ptr, huc_data + full_words * 4, tail_bytes);
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
    if (!ctx) return NTEL_FW_ERR_LOAD_FAILED;
    if (ctx->mmio_base) return NTEL_FW_SUCCESS;

    ctx->mmio_size = 0x10000;
    ctx->mmio_base = malloc(ctx->mmio_size);
    if (!ctx->mmio_base) {
        ctx->mmio_size = 0;
        return NTEL_FW_ERR_LOAD_FAILED;
    }
    memset(ctx->mmio_base, 0, ctx->mmio_size);
    return NTEL_FW_SUCCESS;
}

static NtelFirmwareResult ntel_fw_check_mmio_space(NtelFirmwareContext *ctx, uint32_t offset, uint32_t len) {
    if (!ctx || !ctx->mmio_base) return NTEL_FW_ERR_LOAD_FAILED;
    if (offset + len > ctx->mmio_size) return NTEL_FW_ERR_LOAD_FAILED;
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx) {
    if (!ctx || !ctx->mmio_base || !ctx->guc_blob.loaded) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    const uint32_t load_offset = INTEL_GUC_LOAD_REG_OFFSET;
    const uint32_t status_offset = INTEL_GUC_STATUS_REG_OFFSET;
    if (ntel_fw_check_mmio_space(ctx, load_offset, ctx->guc_blob.size) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, status_offset, 4) != NTEL_FW_SUCCESS) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    memcpy((uint8_t *)ctx->mmio_base + load_offset, ctx->guc_blob.data, ctx->guc_blob.size);
    *(uint32_t *)((uint8_t *)ctx->mmio_base + status_offset) = 0x1;
    printf("[FW] GuC upload simulated (%u bytes)\n", ctx->guc_blob.size);
    return NTEL_FW_SUCCESS;
}

NtelFirmwareResult ntel_fw_upload_huc_to_hw(NtelFirmwareContext *ctx) {
    if (!ctx || !ctx->mmio_base || !ctx->huc_blob.loaded) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    const uint32_t load_offset = INTEL_GUC_LOAD_REG_OFFSET + 0x1000;
    const uint32_t status_offset = INTEL_HUC_STATUS_REG_OFFSET;
    if (ntel_fw_check_mmio_space(ctx, load_offset, ctx->huc_blob.size) != NTEL_FW_SUCCESS ||
        ntel_fw_check_mmio_space(ctx, status_offset, 4) != NTEL_FW_SUCCESS) {
        return NTEL_FW_ERR_LOAD_FAILED;
    }

    memcpy((uint8_t *)ctx->mmio_base + load_offset, ctx->huc_blob.data, ctx->huc_blob.size);
    *(uint32_t *)((uint8_t *)ctx->mmio_base + status_offset) = 0x1;
    printf("[FW] HuC upload simulated (%u bytes)\n", ctx->huc_blob.size);
    return NTEL_FW_SUCCESS;
}
#endif
