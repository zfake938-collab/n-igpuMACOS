#include "NtelSharedRing.h"
#include <string.h>
#include <stdio.h>

#ifdef __APPLE__
#include <libkern/OSAtomic.h>
#define NTEL_MEMORY_BARRIER() OSMemoryBarrier()
#else
#define NTEL_MEMORY_BARRIER() __sync_synchronize()
#endif

bool ntel_ring_init(NtelRingContext *ctx, void *shared_mem, uint32_t size) {
    if (!ctx || !shared_mem) return false;
    if (size < sizeof(struct NtelSharedRingHeader) || size > NTEL_RING_MAX_CAPACITY) {
        return false;
    }

    ctx->header = (struct NtelSharedRingHeader *)shared_mem;
    ctx->buffer_base = (uint8_t *)shared_mem + sizeof(struct NtelSharedRingHeader);
    ctx->capacity_bytes = size - sizeof(struct NtelSharedRingHeader);

    ctx->header->writeIdx = 0;
    ctx->header->readIdx = 0;
    ctx->header->capacityDW = ctx->capacity_bytes / 4;
    memset(ctx->header->reserved, 0, sizeof(ctx->header->reserved));

    NTEL_MEMORY_BARRIER();

    return true;
}

bool ntel_ring_try_write(NtelRingContext *ctx, const uint8_t *data, uint32_t len) {
    if (len == 0 || len > ctx->capacity_bytes) return false;
    if (ctx == NULL || ctx->header == NULL || ctx->buffer_base == NULL || data == NULL) return false;

    uint32_t current_w = ctx->header->writeIdx;
    uint32_t current_r = ctx->header->readIdx;

    uint32_t used;
    if (current_w >= current_r) {
        used = current_w - current_r;
    } else {
        used = ctx->capacity_bytes - (current_r - current_w);
    }

    uint32_t free_space = ctx->capacity_bytes - used;

    if (free_space == 0) return false;
    if (len >= free_space) return false;

    uint32_t space_to_end = ctx->capacity_bytes - current_w;

    if (len <= space_to_end) {
        memcpy(ctx->buffer_base + current_w, data, len);
    } else {
        memcpy(ctx->buffer_base + current_w, data, space_to_end);
        memcpy(ctx->buffer_base, data + space_to_end, len - space_to_end);
    }

    NTEL_MEMORY_BARRIER();

    ctx->header->writeIdx = (current_w + len) % ctx->capacity_bytes;

    NTEL_MEMORY_BARRIER();

    return true;
}

bool ntel_ring_try_read(NtelRingContext *ctx, uint8_t *out_data, uint32_t max_len, uint32_t *bytes_read) {
    if (ctx == NULL || ctx->header == NULL || ctx->buffer_base == NULL || out_data == NULL || bytes_read == NULL) return false;

    uint32_t current_w = ctx->header->writeIdx;
    uint32_t current_r = ctx->header->readIdx;

    NTEL_MEMORY_BARRIER();

    if (current_w == current_r) {
        *bytes_read = 0;
        return false;
    }

    uint32_t available;
    if (current_w >= current_r) {
        available = current_w - current_r;
    } else {
        available = ctx->capacity_bytes - (current_r - current_w);
    }

    uint32_t to_read = (available < max_len) ? available : max_len;
    uint32_t space_to_end = ctx->capacity_bytes - current_r;

    if (to_read <= space_to_end) {
        memcpy(out_data, ctx->buffer_base + current_r, to_read);
    } else {
        memcpy(out_data, ctx->buffer_base + current_r, space_to_end);
        memcpy(out_data + space_to_end, ctx->buffer_base, to_read - space_to_end);
    }

    NTEL_MEMORY_BARRIER();

    ctx->header->readIdx = (current_r + to_read) % ctx->capacity_bytes;

    NTEL_MEMORY_BARRIER();

    *bytes_read = to_read;

    return true;
}

void ntel_ring_cleanup(NtelRingContext *ctx) {
    if (ctx) {
        ctx->header = NULL;
        ctx->buffer_base = NULL;
        ctx->capacity_bytes = 0;
    }
}
