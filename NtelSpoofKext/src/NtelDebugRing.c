#include "NtelDebugRing.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#if defined(__APPLE__)
#include <mach/mach_time.h>
static uint64_t ntel_debug_now_ns_impl(void) {
    static mach_timebase_info_data_t s_timebase;
    static int s_init = 0;
    if (!s_init) {
        s_timebase.denom = 0;
        s_timebase.numer = 0;
        (void)mach_timebase_info(&s_timebase);
        s_init = 1;
    }
    uint64_t t = mach_absolute_time();
    if (s_timebase.denom == 0) return 0;
    return (t * s_timebase.numer) / s_timebase.denom;
}
#else
#include <time.h>
#include <sys/time.h>
static uint64_t ntel_debug_now_ns_impl(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    }
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000000000ull + (uint64_t)tv.tv_usec * 1000ull;
}
#endif

ntel_debug_ring_t g_ntel_debug_ring;

void ntel_debug_ring_init(ntel_debug_ring_t *ring) {
    if (!ring) return;
    atomic_store(&ring->write_index, 0);
    memset(&ring->records[0], 0, sizeof(ring->records));
}

uint64_t ntel_debug_now_ns(void) {
    return ntel_debug_now_ns_impl();
}

void ntel_debug_ring_append(ntel_debug_ring_t *ring,
                           uint16_t component,
                           uint16_t event_id,
                           uint32_t read_idx,
                           uint32_t write_idx_val,
                           uint32_t fence_value,
                           uint32_t active_pid,
                           int32_t last_error) {
    if (!ring) return;

    /* Build the complete record on the stack before publishing it.
       This prevents a reader from observing a partially-written (torn) record
       between the slot reservation and the field stores. */
    ntel_debug_record_t rec_local;
    rec_local.timestamp   = ntel_debug_now_ns_impl();
    rec_local.component   = component;
    rec_local.event_id    = event_id;
    rec_local.read_idx    = read_idx;
    rec_local.write_idx   = write_idx_val;
    rec_local.fence_value = fence_value;
    rec_local.active_pid  = active_pid;
    rec_local.last_error  = last_error;

    /* Reserve a slot.  All fields are ready on the stack at this point. */
    uint32_t seq = atomic_fetch_add_explicit(&ring->write_index, 1u, memory_order_relaxed);
    rec_local.sequence = seq;

    uint32_t slot = seq & (NTEL_DEBUG_RING_SIZE - 1u);

    /* Copy the complete record into the ring in a single store, then issue
       a release fence so readers that acquire on write_index see fully-written
       fields (the ring is a flight-recorder that tolerates overwrite, so we
       still copy even if the slot may already have been wrapped). */
    ring->records[slot] = rec_local;
    atomic_thread_fence(memory_order_release);
}

int ntel_debug_record_format(char *buf, size_t buflen, const ntel_debug_record_t *rec) {
    if (!buf || !rec || buflen == 0) return -1;

    const char *comp_names[] = {"RING", "CACHE", "FW", "SPOOF", "ENGINE"};
    /* Event IDs start at NTEL_DEBUG_BASE_EVENT_ID+1 (1001) through BASE+4 (1004).
       Index 0 is unused; the enum has no BASE+0 event, so we skip it explicitly. */
    const char *evt_names[] = {
        /* +1 */ "CACHE_COLLISION_MISMATCH",
        /* +2 */ "SCORCH_PASS",
        /* +3 */ "PID_MISMATCH",
        /* +4 */ "TRANSLATION_FAILED"
    };
    static const uint32_t EVT_COUNT = 4;

    const char *comp = (rec->component < 5) ? comp_names[rec->component] : "UNKN";

    const char *evt = "UNKN";  /* default for unrecognised event IDs */
    if (rec->event_id >= NTEL_DEBUG_BASE_EVENT_ID + 1 &&
        rec->event_id <= NTEL_DEBUG_BASE_EVENT_ID + EVT_COUNT) {
        evt = evt_names[rec->event_id - (NTEL_DEBUG_BASE_EVENT_ID + 1)];
    }

    return snprintf(buf, buflen,
        "[%llu ns] seq=%u %s:%s ridx=%u widx=%u fence=%u pid=%u err=%d",
        (unsigned long long)rec->timestamp,
        rec->sequence,
        comp, evt,
        rec->read_idx, rec->write_idx,
        rec->fence_value, rec->active_pid,
        rec->last_error);
}

uint32_t ntel_debug_ring_snapshot(ntel_debug_ring_t *ring) {
    if (!ring) return 0;
    return atomic_load_explicit(&ring->write_index, memory_order_acquire);
}

uint32_t ntel_debug_ring_read(ntel_debug_ring_t *ring, ntel_debug_record_t *out,
                              uint32_t start_seq, uint32_t count) {
    if (!ring || !out || count == 0) return 0;

    uint32_t write_idx = atomic_load_explicit(&ring->write_index, memory_order_acquire);
    if (start_seq >= write_idx) return 0;

    uint32_t available = write_idx - start_seq;
    uint32_t to_read = (count < available) ? count : available;

    for (uint32_t i = 0; i < to_read; i++) {
        uint32_t seq = start_seq + i;
        uint32_t slot = seq & (NTEL_DEBUG_RING_SIZE - 1u);
        out[i] = ring->records[slot];
    }

    return to_read;
}

void ntel_debug_ring_reset(ntel_debug_ring_t *ring) {
    if (!ring) return;
    atomic_store(&ring->write_index, 0);
    memset(&ring->records[0], 0, sizeof(ring->records));
}

ntel_debug_ring_t *ntel_debug_ring_get_mapped(void) {
    return &g_ntel_debug_ring;
}