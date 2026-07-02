#ifndef NTEL_SHARED_RING_H
#define NTEL_SHARED_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#if !defined(__APPLE__) || defined(NTEL_USERMODE)
#include <pthread.h>
#endif

typedef struct {
    _Atomic(uint32_t) writeIdx;  // Atomic producer offset (byte-based)
    _Atomic(uint32_t) readIdx;   // Consumer offset (byte-based)
    uint32_t capacityDW;         // Total ring capacity in Double Words (informational)
    uint32_t reserved[13];       // Padding for 64-byte alignment
} NtelSharedRingHeader;

#define NTEL_RING_ALIGNMENT 64
#define NTEL_RING_MAX_CAPACITY (8 * 1024 * 1024) // 8MB as per EVP

typedef struct {
    NtelSharedRingHeader *header;
    uint8_t *buffer_base;
    uint32_t capacity_bytes;
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
    void *lock;
#else
    pthread_mutex_t lock;
    bool lock_initialized;
#endif
} NtelRingContext;

// Core API
bool ntel_ring_init(NtelRingContext *ctx, void *shared_mem, uint32_t size);
bool ntel_ring_try_write(NtelRingContext *ctx, const uint8_t *data, uint32_t len);
bool ntel_ring_try_read(NtelRingContext *ctx, uint8_t *out_data, uint32_t max_len, uint32_t *bytes_read);
void ntel_ring_cleanup(NtelRingContext *ctx);

#endif // NTEL_SHARED_RING_H
