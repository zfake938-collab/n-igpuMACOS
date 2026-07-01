#define _POSIX_C_SOURCE 200809L
#include "NtelDebugRing.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <inttypes.h>

static volatile bool keep_running = true;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = false;
}

static void poll_sleep(void) {
    struct timespec req = {0, 10000000L};
    nanosleep(&req, NULL);
}

int main(void) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    ntel_debug_ring_t *ring = ntel_debug_ring_get_mapped();

    printf("[DAEMON] NtelDebugRing consumer started.\n");
    printf("[DAEMON] Ring base: %p, capacity: %d records\n",
           (void*)ring, NTEL_DEBUG_RING_SIZE);

    uint64_t expected_seq = 0;
    uint32_t last_seen_write_idx = 0;
    bool first_run = true;
    uint64_t total_drops = 0;
    uint64_t records_processed = 0;

    while (keep_running) {
        uint32_t current_write_idx = ntel_debug_ring_snapshot(ring);

        if (current_write_idx == last_seen_write_idx && !first_run) {
            poll_sleep();
            continue;
        }

        if (first_run) {
            /* Start reading from as far back as the ring holds, but avoid
               unsigned underflow: if fewer entries have been written than the
               ring capacity, start from the very beginning (seq 0). */
            if (current_write_idx >= NTEL_DEBUG_RING_SIZE) {
                last_seen_write_idx = current_write_idx - NTEL_DEBUG_RING_SIZE;
            } else {
                last_seen_write_idx = 0;
            }
            uint32_t start_slot = last_seen_write_idx & (NTEL_DEBUG_RING_SIZE - 1);
            expected_seq = (uint64_t)ring->records[start_slot].sequence;
            first_run = false;
            printf("[DAEMON] Synchronized to sequence %" PRIu64 "\n", expected_seq);
        }

        while (last_seen_write_idx != current_write_idx && keep_running) {
            uint32_t slot = last_seen_write_idx & (NTEL_DEBUG_RING_SIZE - 1);
            ntel_debug_record_t record = ring->records[slot];

            if (record.sequence > expected_seq) {
                uint64_t dropped = (uint64_t)record.sequence - expected_seq;
                total_drops += dropped;
                printf("[DAEMON] Overrun: Lost %" PRIu64 " records (seq gap %" PRIu64 " -> %" PRIu32 ")\n",
                       dropped, expected_seq, record.sequence);
                expected_seq = record.sequence;
            } else if (record.sequence < expected_seq) {
                printf("[DAEMON] CRITICAL: Double wrap detected. Resynchronizing.\n");
                if (current_write_idx >= NTEL_DEBUG_RING_SIZE) {
                    last_seen_write_idx = current_write_idx - NTEL_DEBUG_RING_SIZE;
                } else {
                    last_seen_write_idx = 0;
                }
                uint32_t rsync_slot = last_seen_write_idx & (NTEL_DEBUG_RING_SIZE - 1);
                expected_seq = (uint64_t)ring->records[rsync_slot].sequence;
                break;
            }

            char buf[256];
            ntel_debug_record_format(buf, sizeof(buf), &record);
            printf("[DAEMON] %s\n", buf);

            expected_seq++;
            last_seen_write_idx++;
            records_processed++;
        }

        if (records_processed > 0 && records_processed % 100 == 0) {
            printf("[DAEMON] Progress: %" PRIu64 " records, %" PRIu64 " drops detected\n",
                   records_processed, total_drops);
        }
    }

    printf("[DAEMON] Shutting down. Total: %" PRIu64 " records processed, %" PRIu64 " drops\n",
           records_processed, total_drops);
    return 0;
}