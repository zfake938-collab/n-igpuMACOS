#include "NtelSharedRing.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#ifdef __APPLE__
#include <libkern/OSAtomic.h>
#include <IOKit/IOLocks.h>
#define NTEL_BARRIER_FULL()    OSMemoryBarrier()
#define NTEL_RING_LOCK(c)      do { if ((c)->lock) IOLockLock((IOLock *)(c)->lock); } while (0)
#define NTEL_RING_UNLOCK(c)    do { if ((c)->lock) IOLockUnlock((IOLock *)(c)->lock); } while (0)
#define NTEL_RING_READY(c)     ((c)->lock != NULL)
#else
#define NTEL_BARRIER_FULL()    __sync_synchronize()
#define NTEL_RING_LOCK(c)      pthread_mutex_lock(&(c)->lock)
#define NTEL_RING_UNLOCK(c)    pthread_mutex_unlock(&(c)->lock)
#define NTEL_RING_READY(c)     ((c)->lock_initialized)
#endif

// Cache coherency for non-snooping GPU paths
// These enforce the contract: Host Write -> Eviction -> Barrier -> Doorbell
#ifdef __APPLE__
#include <mach/mach_time.h>
static inline void ntel_cache_evict(const void *addr, uint32_t len) {
    // macOS kernel: use processor cache control via __builtin_ia32_clflush
    const uint8_t *ptr = (const uint8_t *)addr;
    const uint8_t *end = ptr + len;
    for (; ptr < end; ptr += 64) {
        __builtin_ia32_clflush(ptr);
    }
}
static inline void ntel_cache_barrier(void) {
    __builtin_ia32_mfence();
}
#else
#include <immintrin.h>
static inline void ntel_cache_evict(const void *addr, uint32_t len) {
    const uint8_t *ptr = (const uint8_t *)addr;
    const uint8_t *end = ptr + len;
    for (; ptr < end; ptr += 64) {
        _mm_clflush(ptr);
    }
}
static inline void ntel_cache_barrier(void) {
    _mm_mfence();
}
#endif

bool ntel_ring_init(NtelRingContext *ctx, void *shared_mem, uint32_t size) {
    if (!ctx || !shared_mem) return false;
    const uint32_t header_size = (uint32_t)sizeof(NtelSharedRingHeader);
    if (size < header_size || size > NTEL_RING_MAX_CAPACITY) {
        return false;
    }

#ifdef __APPLE__
    ctx->lock = IOLockAlloc();
    if (!ctx->lock) return false;
#else
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        ctx->lock_initialized = false;
        return false;
    }
    ctx->lock_initialized = true;
#endif

    ctx->header = (NtelSharedRingHeader *)shared_mem;
    ctx->buffer_base = (uint8_t *)shared_mem + header_size;
    ctx->capacity_bytes = size - header_size;

    atomic_store_explicit(&ctx->header->writeIdx, 0, memory_order_relaxed);
    atomic_store_explicit(&ctx->header->readIdx,  0, memory_order_relaxed);
    ctx->header->capacityDW = ctx->capacity_bytes / 4;
    memset(ctx->header->reserved, 0, sizeof(ctx->header->reserved));

    NTEL_BARRIER_FULL();

    return true;
}

bool ntel_ring_try_write(NtelRingContext *ctx, const uint8_t *data, uint32_t len) {
    if (ctx == NULL || ctx->header == NULL || ctx->buffer_base == NULL || data == NULL) return false;
    if (!NTEL_RING_READY(ctx)) return false;
    if (len == 0 || len > ctx->capacity_bytes) return false;

    bool wrote = false;
    NTEL_RING_LOCK(ctx);

    uint32_t current_w = atomic_load_explicit(&ctx->header->writeIdx, memory_order_relaxed);
    uint32_t current_r = atomic_load_explicit(&ctx->header->readIdx,  memory_order_relaxed);

    uint32_t used;
    if (current_w >= current_r) {
        used = current_w - current_r;
    } else {
        used = ctx->capacity_bytes - (current_r - current_w);
    }

    uint32_t free_space = ctx->capacity_bytes - used;

    if (free_space == 0) goto out;
    if (len >= free_space) goto out;

    uint32_t space_to_end = ctx->capacity_bytes - current_w;

    if (len <= space_to_end) {
        memcpy(ctx->buffer_base + current_w, data, len);
    } else {
        memcpy(ctx->buffer_base + current_w, data, space_to_end);
        memcpy(ctx->buffer_base, data + space_to_end, len - space_to_end);
    }

    // Step 1: Evict - Flush the modified cache lines for GPU visibility
    ntel_cache_evict(ctx->buffer_base + current_w, 
                    len <= space_to_end ? len : space_to_end);
    if (len > space_to_end) {
        ntel_cache_evict(ctx->buffer_base, len - space_to_end);
    }

    // Step 2: Barrier - Prevent reordering before doorbell
    ntel_cache_barrier();

    /* RELEASE barrier: all data stores must be globally visible
       before the write index update is observed by the reader. */
    NTEL_BARRIER_FULL();

    atomic_store_explicit(&ctx->header->writeIdx,
                          (current_w + len) % ctx->capacity_bytes,
                          memory_order_release);

    wrote = true;

out:
    NTEL_RING_UNLOCK(ctx);
    return wrote;
}

bool ntel_ring_try_read(NtelRingContext *ctx, uint8_t *out_data, uint32_t max_len, uint32_t *bytes_read) {
    if (ctx == NULL || ctx->header == NULL || ctx->buffer_base == NULL || out_data == NULL || bytes_read == NULL) return false;
    if (!NTEL_RING_READY(ctx)) return false;
    if (max_len == 0) {
        *bytes_read = 0;
        return false;
    }

    bool read = false;
    *bytes_read = 0;
    NTEL_RING_LOCK(ctx);

    /* ACQUIRE barrier: ensure write index and all payload writes are
       visible before we read any data from the buffer. */
    uint32_t current_w = atomic_load_explicit(&ctx->header->writeIdx, memory_order_acquire);
    uint32_t current_r = atomic_load_explicit(&ctx->header->readIdx,  memory_order_relaxed);

    if (current_w == current_r) {
        goto out;
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

    /* RELEASE barrier: all data loads must complete before the read
       index update signals to the writer that space is reclaimed. */
    NTEL_BARRIER_FULL();

    atomic_store_explicit(&ctx->header->readIdx,
                          (current_r + to_read) % ctx->capacity_bytes,
                          memory_order_release);

    *bytes_read = to_read;
    read = true;

out:
    NTEL_RING_UNLOCK(ctx);
    return read;
}

void ntel_ring_cleanup(NtelRingContext *ctx) {
    if (ctx) {
#ifdef __APPLE__
        if (ctx->lock) {
            IOLockFree((IOLock *)ctx->lock);
        }
        ctx->lock = NULL;
#else
        if (ctx->lock_initialized) {
            pthread_mutex_destroy(&ctx->lock);
        }
        ctx->lock_initialized = false;
#endif
        ctx->header = NULL;
        ctx->buffer_base = NULL;
        ctx->capacity_bytes = 0;
    }
}
