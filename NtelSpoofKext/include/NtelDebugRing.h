#ifndef NTEL_DEBUG_RING_H
#define NTEL_DEBUG_RING_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/*
 * Lock-free fixed-size debug ring (Flight Recorder Mode)
 * - Fixed capacity power-of-two
 * - Silent overwrite on full (never blocks, never waits)
 * - Packed binary records (no formatting in hot path)
 * - Consumer detects drops via sequence gaps (user-space responsibility)
 *
 * Intended for hot-path instrumentation where timing distortions must be avoided.
 */

#ifndef NTEL_DEBUG
#define NTEL_DEBUG 0
#endif

#define NTEL_DEBUG_BASE_EVENT_ID 1000

typedef enum ntel_debug_component_t {
    NTEL_COMP_RING  = 0,
    NTEL_COMP_CACHE = 1,
    NTEL_COMP_FW    = 2,
    NTEL_COMP_SPOOF = 3,
    NTEL_COMP_ENGINE = 4
} ntel_debug_component_t;

typedef enum ntel_debug_event_id_t {
    NTEL_EVT_CACHE_COLLISION_MISMATCH = NTEL_DEBUG_BASE_EVENT_ID + 1,
    NTEL_EVT_SCORCH_PASS              = NTEL_DEBUG_BASE_EVENT_ID + 2,
    NTEL_EVT_PID_MISMATCH             = NTEL_DEBUG_BASE_EVENT_ID + 3,
    NTEL_EVT_TRANSLATION_FAILED       = NTEL_DEBUG_BASE_EVENT_ID + 4
} ntel_debug_event_id_t;

#if defined(__GNUC__) || defined(__clang__)
#define NTEL_PACKED __attribute__((packed))
#else
#define NTEL_PACKED
#endif

typedef struct NTEL_PACKED ntel_debug_record_t {
    uint64_t timestamp;
    uint32_t sequence;
    uint16_t component;
    uint16_t event_id;
    uint32_t read_idx;
    uint32_t write_idx;
    uint32_t fence_value;
    uint32_t active_pid;
    int32_t  last_error;
} ntel_debug_record_t;

#ifndef NTEL_DEBUG_RING_SIZE
#define NTEL_DEBUG_RING_SIZE 1024u
#endif

#if (NTEL_DEBUG_RING_SIZE & (NTEL_DEBUG_RING_SIZE - 1u)) != 0u
#error "NTEL_DEBUG_RING_SIZE must be a power-of-two"
#endif

typedef struct ntel_debug_ring_t {
    _Atomic(uint32_t) write_index;
    ntel_debug_record_t records[NTEL_DEBUG_RING_SIZE];
} ntel_debug_ring_t;

extern ntel_debug_ring_t g_ntel_debug_ring;

void ntel_debug_ring_init(ntel_debug_ring_t *ring);
void ntel_debug_ring_append(ntel_debug_ring_t *ring,
                           uint16_t component,
                           uint16_t event_id,
                           uint32_t read_idx,
                           uint32_t write_idx,
                           uint32_t fence_value,
                           uint32_t active_pid,
                           int32_t last_error);
uint64_t ntel_debug_now_ns(void);
int ntel_debug_record_format(char *buf, size_t buflen, const ntel_debug_record_t *rec);
uint32_t ntel_debug_ring_snapshot(ntel_debug_ring_t *ring);
uint32_t ntel_debug_ring_read(ntel_debug_ring_t *ring, ntel_debug_record_t *out,
                              uint32_t start_seq, uint32_t count);
void ntel_debug_ring_reset(ntel_debug_ring_t *ring);
ntel_debug_ring_t *ntel_debug_ring_get_mapped(void);

#define NTEL_DEBUG_LLDB_DUMP(ring_ptr) \
    do { \
        ntel_debug_record_t tmp_rec; \
        uint32_t tmp_seq = 0, tmp_cnt; \
        char tmp_buf[256]; \
        while ((tmp_cnt = ntel_debug_ring_read((ring_ptr), &tmp_rec, tmp_seq, 1)) > 0) { \
            ntel_debug_record_format(tmp_buf, sizeof(tmp_buf), &tmp_rec); \
            printf("%s\n", tmp_buf); \
            tmp_seq++; \
        } \
    } while (0)

#if NTEL_DEBUG >= 1
#define NTEL_LOG_SPARSE(_component, _event_id, _read_idx, _write_idx, _fence, _pid, _err) \
    do { ntel_debug_ring_append(&g_ntel_debug_ring, \
                               (uint16_t)(_component), (uint16_t)(_event_id), \
                               (_read_idx), (_write_idx), (_fence), (_pid), (_err)); } while (0)
#else
#define NTEL_LOG_SPARSE(_component, _event_id, _read_idx, _write_idx, _fence, _pid, _err) \
    do {} while (0)
#endif

#if NTEL_DEBUG >= 2
#define NTEL_LOG_HOT(_component, _event_id, _read_idx, _write_idx, _fence, _pid, _err) \
    do { ntel_debug_ring_append(&g_ntel_debug_ring, \
                               (uint16_t)(_component), (uint16_t)(_event_id), \
                               (_read_idx), (_write_idx), (_fence), (_pid), (_err)); } while (0)
#else
#define NTEL_LOG_HOT(_component, _event_id, _read_idx, _write_idx, _fence, _pid, _err) \
    do {} while (0)
#endif

#endif /* NTEL_DEBUG_RING_H */