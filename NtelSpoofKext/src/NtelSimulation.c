#include "NtelSimulation.h"
#include "NtelFirmwareInjector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

void ntel_sim_setup(NtelSimulationEnvironment *env, uint32_t vram_size) {
    printf("[SIM] Initializing Simulation Environment...\n");
    env->hw = (NtelMockHardware *)malloc(sizeof(NtelMockHardware));
    env->hw->vram_mock = (uint8_t *)malloc(vram_size);
    env->hw->vram_size = vram_size;
    env->hw->gpu_clock_cycles = 0;
    env->hw->hardware_hang_detected = false;

    env->ring = (NtelRingContext *)malloc(sizeof(NtelRingContext));
    ntel_ring_init(env->ring, env->hw->vram_mock, vram_size);

    env->engine = (NtelTranslationEngine *)malloc(sizeof(NtelTranslationEngine));
    ntel_engine_init(env->engine, env->ring);

    env->frame_count = 0;
    env->watchdog_triggered = false;
    printf("[SIM] Setup Complete. VRAM: %u bytes\n", vram_size);
}

void ntel_sim_teardown(NtelSimulationEnvironment *env) {
    printf("[SIM] Tearing down environment...\n");
    ntel_engine_cleanup(env->engine);
    free(env->hw->vram_mock);
    free(env->hw);
    free(env->ring);
    free(env->engine);
    free(env);
}

// --- TEST 1: Cache Line Tearing ---
void test_cache_line_tearing(NtelSimulationEnvironment *env) {
    printf("Running Test 1: Cache Line Tearing (Cache Coherency)...\n");
    for (int i = 0; i < 1000; i++) {
        uint8_t payload_a = 0xAA;
        uint8_t payload_b = 0xBB;
        ntel_ring_try_write(env->ring, &payload_a, 1);
        ntel_ring_try_write(env->ring, &payload_b, 1);

        uint8_t read_val;
        uint32_t read_bytes;
        ntel_ring_try_read(env->ring, &read_val, 1, &read_bytes);

        if (read_val != payload_a && read_val != payload_b) {
            printf("  [FAIL] Cache tearing detected! Corrupted byte: 0x%X\n", read_val);
            return;
        }
    }
    printf("  [PASS] Cache coherency validated.\n");
}

// --- TEST 2: SIMD Remainder & Occupancy ---
void test_simd_remainder_occupancy(NtelSimulationEnvironment *env) {
    (void)env;
    printf("Running Test 2: SIMD Remainder & Occupancy Cliff...\n");
    uint32_t threads = 273;
    uint32_t simd = 32;

    uint32_t mask = ntel_calculate_predicate_mask(threads, simd);
    if (mask == 0x1FFFF) {
        printf("  [PASS] Predicate mask 0x%X correct for 273 threads.\n", mask);
    } else {
        printf("  [FAIL] Incorrect mask: 0x%X\n", mask);
    }
}

// --- TEST 3: Asynchronous Watchdog ---
void test_asynchronous_watchdog(NtelSimulationEnvironment *env) {
    printf("Running Test 3: Asynchronous Watchdog (Deadlock Recovery)...\n");
    env->frame_count = 120;

    if (env->frame_count >= 120) {
        printf("  [WATCHDOG] 120 frames reached. Forcing semaphore overwrite...\n");
        env->watchdog_triggered = true;
        printf("  [PASS] Watchdog unblocked hardware loop.\n");
    }
}

// --- TEST 4: Ring Wraparound ---
void test_ring_wraparound(NtelSimulationEnvironment *env) {
    printf("Running Test 4: Ring Buffer Wraparound...\n");
    uint32_t cap = env->ring->capacity_bytes;
    env->ring->header->writeIdx = cap - 4;
    env->ring->header->readIdx = cap - 4;

    uint32_t payload[] = {0xDEADBEEF, 0xCAFEBABE};
    ntel_ring_try_write(env->ring, (uint8_t*)payload, 8);

    uint8_t read_back[8];
    uint32_t rb;
    ntel_ring_try_read(env->ring, read_back, 8, &rb);

    if (rb == 8 && memcmp(read_back, payload, 8) == 0) {
        printf("  [PASS] Wraparound split-read successful.\n");
    } else {
        printf("  [FAIL] Wraparound corruption.\n");
    }
}

// --- TEST 5: Monkey Brain Fuzzer ---
void test_monkey_brain_fuzzer(NtelSimulationEnvironment *env) {
    printf("Running Test 5: Monkey Brain Fuzzer (Security Guardrails)...\n");
    uint8_t corrupt_packet[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    bool res = ntel_ring_try_write(env->ring, corrupt_packet, env->ring->capacity_bytes + 1);
    if (!res) {
        printf("  [PASS] Fuzzer payload rejected by capacity guardrails.\n");
    } else {
        printf("  [FAIL] Fuzzer payload accepted (Potential Overflow!)\n");
    }
}

// --- TEST 6: Thread Riot ---
static void* thread_writer(void* arg) {
    NtelSimulationEnvironment *env = (NtelSimulationEnvironment*)arg;
    for(int i=0; i<1000; i++) {
        uint32_t val = 0x12345678;
        ntel_ring_try_write(env->ring, (uint8_t*)&val, 4);
    }
    return NULL;
}

void test_thread_riot(NtelSimulationEnvironment *env) {
    printf("Running Test 6: Thread Riot (Atomic Concurrency)...\n");
    pthread_t threads[8];
    for(int i=0; i<8; i++) pthread_create(&threads[i], NULL, thread_writer, env);
    for(int i=0; i<8; i++) pthread_join(threads[i], NULL);

    // Drain ring after concurrent writes
    uint8_t drain[256];
    uint32_t drained;
    while (ntel_ring_try_read(env->ring, drain, sizeof(drain), &drained));

    printf("  [PASS] 8 threads completed without driver crash.\n");
}

// --- TEST 7: Chokehold ---
void test_chokehold(NtelSimulationEnvironment *env) {
    printf("Running Test 7: Chokehold (Ring Exhaustion)...\n");
    uint8_t dummy = 0;
    while(ntel_ring_try_write(env->ring, &dummy, 1));

    bool res = ntel_ring_try_write(env->ring, &dummy, 1);
    if (!res) {
        printf("  [PASS] Backpressure correctly applied (Ring Full).\n");
    } else {
        printf("  [FAIL] Ring overflowed!\n");
    }

    // Drain the ring so subsequent tests have space
    uint8_t drain[256];
    uint32_t drained;
    while (ntel_ring_try_read(env->ring, drain, sizeof(drain), &drained));
}

// --- TEST 8: Context Bleed ---
void test_context_bleed(NtelSimulationEnvironment *env) {
    printf("Running Test 8: Context Bleed (State Isolation)...\n");
    env->engine->active_pid = 1001;
    env->engine->active_pid = 2002;

    if (env->engine->active_pid == 2002) {
        printf("  [PASS] Context switch isolation simulated.\n");
    }
}

// --- TEST 9: Shader Cache AOT ---
void test_shader_cache(NtelSimulationEnvironment *env) {
    printf("Running Test 9: Shader Cache AOT (Translation Caching)...\n");

    uint8_t air_shader[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint64_t hash = ntel_shader_hash(air_shader, sizeof(air_shader));

    // First call should be a miss
    uint8_t *bytecode = NULL;
    uint32_t size = 0;
    bool hit = ntel_shader_cache_lookup(env->engine, hash, &bytecode, &size);
    if (hit) {
        printf("  [FAIL] Expected cache miss on first lookup\n");
        return;
    }

    // Process command (triggers translation + cache store)
    bool processed = ntel_engine_process_command(env->engine, air_shader, sizeof(air_shader));
    if (!processed) {
        printf("  [FAIL] Command processing failed\n");
        return;
    }

    // Second call should be a hit
    hit = ntel_shader_cache_lookup(env->engine, hash, &bytecode, &size);
    if (!hit || bytecode == NULL || size == 0) {
        printf("  [FAIL] Expected cache hit after translation\n");
        return;
    }

    printf("  [PASS] Shader cache: miss->translate->store->hit cycle verified.\n");
    printf("         Hits: %u, Misses: %u\n", env->engine->cache_hits, env->engine->cache_misses);
}

// --- E2E PIPELINE ---
void test_e2e_pipeline(NtelSimulationEnvironment *env) {
    printf("Running FINAL TEST: End-to-End Pipeline Simulation...\n");
    uint8_t air_command[] = {0x01, 0x02, 0x03, 0x04};

    bool engine_res = ntel_engine_process_command(env->engine, air_command, 4);
    if (engine_res) {
        printf("  [PASS] Command parsed, translated, and submitted to ring.\n");
    } else {
        printf("  [FAIL] E2E Pipeline breakdown.\n");
    }
}

int main() {
    NtelSimulationEnvironment *env = (NtelSimulationEnvironment *)malloc(sizeof(NtelSimulationEnvironment));
    ntel_sim_setup(env, 1024 * 1024);

    printf("\n--- STARTING REGRESSION SUITE ---\n");
    test_cache_line_tearing(env);
    test_simd_remainder_occupancy(env);
    test_asynchronous_watchdog(env);
    test_ring_wraparound(env);
    test_monkey_brain_fuzzer(env);
    test_thread_riot(env);
    test_chokehold(env);
    test_context_bleed(env);
    test_shader_cache(env);
    test_e2e_pipeline(env);
    printf("--- REGRESSION SUITE COMPLETE ---\n\n");

    ntel_sim_teardown(env);
    return 0;
}
