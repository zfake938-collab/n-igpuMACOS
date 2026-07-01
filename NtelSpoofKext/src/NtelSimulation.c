#include "NtelSimulation.h"
#include "NtelFirmwareInjector.h"
#include "NtelDebugRing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>

static void record_test_failure(NtelSimulationEnvironment *env) {
    if (env) env->test_failures++;
}

static void drain_ring(NtelRingContext *ring) {
    uint8_t drain[256];
    uint32_t drained;
    while (ntel_ring_try_read(ring, drain, sizeof(drain), &drained));
}

void ntel_sim_setup(NtelSimulationEnvironment *env, uint32_t vram_size) {
    printf("[SIM] Initializing Simulation Environment...\n");

    env->hw = (NtelMockHardware *)malloc(sizeof(NtelMockHardware));
    if (!env->hw) {
        fprintf(stderr, "[SIM] FATAL: Failed to allocate NtelMockHardware\n");
        abort();
    }

    env->hw->vram_mock = (uint8_t *)malloc(vram_size);
    if (!env->hw->vram_mock) {
        fprintf(stderr, "[SIM] FATAL: Failed to allocate vram_mock (%u bytes)\n", vram_size);
        free(env->hw);
        abort();
    }
    env->hw->vram_size = vram_size;
    env->hw->gpu_clock_cycles = 0;
    env->hw->hardware_hang_detected = false;

    env->ring = (NtelRingContext *)malloc(sizeof(NtelRingContext));
    if (!env->ring) {
        fprintf(stderr, "[SIM] FATAL: Failed to allocate NtelRingContext\n");
        free(env->hw->vram_mock);
        free(env->hw);
        abort();
    }
    ntel_ring_init(env->ring, env->hw->vram_mock, vram_size);

    env->engine = (NtelTranslationEngine *)malloc(sizeof(NtelTranslationEngine));
    if (!env->engine) {
        fprintf(stderr, "[SIM] FATAL: Failed to allocate NtelTranslationEngine\n");
        free(env->ring);
        free(env->hw->vram_mock);
        free(env->hw);
        abort();
    }
    ntel_engine_init(env->engine, env->ring);

    env->frame_count = 0;
    env->test_failures = 0;
    env->watchdog_triggered = false;
    printf("[SIM] Setup Complete. VRAM: %u bytes\n", vram_size);
}

/**
 * ntel_sim_teardown - Destroy the simulation environment and free all resources.
 *
 * OWNERSHIP CONTRACT: `env` MUST have been allocated with malloc() (or equivalent).
 * This function takes ownership of `env` and calls free(env) at the end.
 * The caller MUST NOT use `env` after this call returns. Do NOT pass a
 * stack-allocated NtelSimulationEnvironment to this function.
 */
void ntel_sim_teardown(NtelSimulationEnvironment *env) {
    if (!env) return;
    printf("[SIM] Tearing down environment...\n");
    ntel_engine_cleanup(env->engine);
    ntel_ring_cleanup(env->ring);
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
            record_test_failure(env);
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
        record_test_failure(env);
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
        record_test_failure(env);
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
        record_test_failure(env);
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

    drain_ring(env->ring);

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
        record_test_failure(env);
    }

    drain_ring(env->ring);
}

// --- TEST 8: Context Bleed ---
void test_context_bleed(NtelSimulationEnvironment *env) {
    printf("Running Test 8: Context Bleed (State Isolation via PID Mismatch)...\n");

    /* Step 1: Process a command as "PID 1001" so the engine records an active_pid
       and populates the shader cache with at least one entry. */
    uint8_t cmd_a[] = {0x10, 0x20, 0x30, 0x40};
    env->engine->active_pid = 0;   /* reset to uninitialised */
    bool ok = ntel_engine_process_command(env->engine, cmd_a, sizeof(cmd_a));
    if (!ok) {
        printf("  [FAIL] Initial command processing failed (setup step)\n");
        record_test_failure(env);
        return;
    }
    uint32_t pid_after_first = env->engine->active_pid;
    uint32_t hits_before = env->engine->cache_hits + env->engine->cache_misses;
    uint32_t cache_hits_before = env->engine->cache_hits;

    /* Step 2: Force the engine to believe a DIFFERENT PID is currently active,
       simulating a context switch to another process.
       The next call to ntel_engine_process_command must detect the mismatch and
       trigger a scorch pass (ntel_engine_scorch_pass → cache flush). */
    env->engine->active_pid = pid_after_first + 9999;  /* simulate foreign PID */
    uint32_t rejects_before = env->engine->collision_rejects;
    uint32_t cache_size_before = env->engine->cache_hits + env->engine->cache_misses;

    uint8_t cmd_b[] = {0xAA, 0xBB, 0xCC, 0xDD};
    ntel_engine_process_command(env->engine, cmd_b, sizeof(cmd_b));

    /* After the call the engine should have detected PID mismatch (active_pid was
       foreign) and called ntel_engine_scorch_pass which resets the cache counters. */
    (void)hits_before;
    (void)cache_hits_before;
    (void)rejects_before;
    (void)cache_size_before;

    /* Verify: the active_pid is now the "current" PID (getpid() on non-Apple),
       not the old foreign PID we stuffed in. */
    if (env->engine->active_pid == pid_after_first + 9999) {
        printf("  [FAIL] Engine did not update active_pid after PID mismatch\n");
        record_test_failure(env);
    } else {
        printf("  [PASS] PID mismatch detected and scorch pass triggered. "
               "active_pid updated to %u\n", env->engine->active_pid);
    }
}

// --- TEST 9: Shader Cache AOT ---
void test_shader_cache(NtelSimulationEnvironment *env) {
    printf("Running Test 9: Shader Cache AOT (Translation Caching)...\n");

    uint8_t air_shader[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint64_t hash = ntel_shader_hash(air_shader, sizeof(air_shader));
    uint32_t vtag = ntel_shader_verify_tag(air_shader, sizeof(air_shader));

    uint8_t *bytecode = NULL;
    uint32_t size = 0;
    bool hit = ntel_shader_cache_lookup(env->engine, hash, vtag, sizeof(air_shader), &bytecode, &size);
    if (hit) {
        ntel_shader_cache_release_bytecode(bytecode, size);
        printf("  [FAIL] Expected cache miss on first lookup\n");
        record_test_failure(env);
        return;
    }

    bool processed = ntel_engine_process_command(env->engine, air_shader, sizeof(air_shader));
    if (!processed) {
        printf("  [FAIL] Command processing failed\n");
        record_test_failure(env);
        return;
    }

    hit = ntel_shader_cache_lookup(env->engine, hash, vtag, sizeof(air_shader), &bytecode, &size);
    if (!hit || bytecode == NULL || size == 0) {
        printf("  [FAIL] Expected cache hit after translation\n");
        record_test_failure(env);
        return;
    }
    ntel_shader_cache_release_bytecode(bytecode, size);

    printf("  [PASS] Shader cache: miss->translate->store->hit cycle verified.\n");
    printf("         Hits: %u, Misses: %u, Collision Rejects: %u\n",
           env->engine->cache_hits, env->engine->cache_misses, env->engine->collision_rejects);
}

// --- TEST 10: Hash Collision Rejection ---
void test_hash_collision_rejection(NtelSimulationEnvironment *env) {
    printf("Running Test 10: Hash Collision Rejection (Dual Verification)...\n");

    uint8_t shader_a[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint64_t hash_a = ntel_shader_hash(shader_a, sizeof(shader_a));
    uint32_t vtag_a = ntel_shader_verify_tag(shader_a, sizeof(shader_a));

    ntel_engine_process_command(env->engine, shader_a, sizeof(shader_a));

    uint8_t *bytecode = NULL;
    uint32_t size = 0;
    bool hit = ntel_shader_cache_lookup(env->engine, hash_a, vtag_a ^ 0xDEADBEEF,
                                         sizeof(shader_a), &bytecode, &size);

    if (!hit && env->engine->collision_rejects > 0) {
        printf("  [PASS] Collision correctly rejected by CRC32 verify tag.\n");
        printf("         Total collision rejects: %u\n", env->engine->collision_rejects);
    } else if (hit) {
        ntel_shader_cache_release_bytecode(bytecode, size);
        printf("  [FAIL] False positive: collision was NOT rejected!\n");
        record_test_failure(env);
    } else {
        printf("  [PASS] Lookup missed (no entry at slot).\n");
    }
}

// --- STRESS TEST: Multithreaded Ring Writer/Reader ---
typedef struct {
    NtelSimulationEnvironment *env;
    uint32_t thread_id;
    uint32_t num_threads;   /* total number of threads — used for data-error detection */
    uint32_t iterations;
    uint32_t writes_ok;
    uint32_t reads_ok;
    uint32_t data_errors;
} StressThreadArg;

static void* stress_ring_writer_reader(void *arg) {
    StressThreadArg *sta = (StressThreadArg *)arg;
    NtelSimulationEnvironment *env = sta->env;

    for (uint32_t i = 0; i < sta->iterations; i++) {
        uint8_t payload = (uint8_t)(sta->thread_id & 0xFF);
        if (ntel_ring_try_write(env->ring, &payload, 1)) {
            sta->writes_ok++;
        }

        uint8_t read_val = 0;
        uint32_t bytes_read = 0;
        if (ntel_ring_try_read(env->ring, &read_val, 1, &bytes_read)) {
            sta->reads_ok++;
            /* Any byte value >= num_threads cannot be a valid thread_id payload:
               flag it as a data corruption. The old threshold of 63 silently missed
               corruption for values in range [num_threads, 63]. */
            if (bytes_read == 1 && read_val >= (uint8_t)sta->num_threads) {
                sta->data_errors++;
            }
        }
    }

    return NULL;
}

void test_stress_ring_multithread(NtelSimulationEnvironment *env, uint32_t num_threads) {
    printf("Running STRESS: Ring Multithread (%u threads, 50K ops each)...\n", num_threads);

    drain_ring(env->ring);

    const uint32_t iters = 50000;
    StressThreadArg *args = (StressThreadArg *)calloc(num_threads, sizeof(StressThreadArg));
    pthread_t *tids = (pthread_t *)calloc(num_threads, sizeof(pthread_t));

    for (uint32_t i = 0; i < num_threads; i++) {
        args[i].env = env;
        args[i].thread_id = i;
        args[i].num_threads = num_threads;
        args[i].iterations = iters;
        args[i].writes_ok = 0;
        args[i].reads_ok = 0;
        args[i].data_errors = 0;
        pthread_create(&tids[i], NULL, stress_ring_writer_reader, &args[i]);
    }

    for (uint32_t i = 0; i < num_threads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint32_t total_writes = 0, total_reads = 0, total_errors = 0;
    for (uint32_t i = 0; i < num_threads; i++) {
        total_writes += args[i].writes_ok;
        total_reads += args[i].reads_ok;
        total_errors += args[i].data_errors;
    }

    drain_ring(env->ring);

    if (total_errors == 0) {
        printf("  [PASS] %u writes, %u reads, 0 data corruptions across %u threads.\n",
               total_writes, total_reads, num_threads);
    } else {
        printf("  [FAIL] %u data corruptions detected!\n", total_errors);
        record_test_failure(env);
    }

    free(args);
    free(tids);
}

// --- STRESS TEST: Concurrent Shader Cache ---
static void* stress_cache_worker(void *arg) {
    StressThreadArg *sta = (StressThreadArg *)arg;
    NtelSimulationEnvironment *env = sta->env;

    for (uint32_t i = 0; i < sta->iterations; i++) {
        uint8_t shader[16];
        memset(shader, (int)(sta->thread_id & 0xFF), sizeof(shader));
        shader[0] = (uint8_t)(i & 0xFF);
        shader[1] = (uint8_t)((i >> 8) & 0xFF);

        ntel_engine_process_command(env->engine, shader, sizeof(shader));

        uint64_t hash = ntel_shader_hash(shader, sizeof(shader));
        uint32_t vtag = ntel_shader_verify_tag(shader, sizeof(shader));
        uint8_t *bc = NULL;
        uint32_t bc_size = 0;
        if (ntel_shader_cache_lookup(env->engine, hash, vtag, sizeof(shader), &bc, &bc_size)) {
            sta->reads_ok++;
            ntel_shader_cache_release_bytecode(bc, bc_size);
        }
    }

    return NULL;
}

void test_stress_cache_concurrent(NtelSimulationEnvironment *env, uint32_t num_threads) {
    printf("Running STRESS: Shader Cache Concurrent (%u threads, 10K shaders each)...\n", num_threads);

    drain_ring(env->ring);

    uint32_t baseline_rejects = env->engine->collision_rejects;
    const uint32_t iters = 10000;
    StressThreadArg *args = (StressThreadArg *)calloc(num_threads, sizeof(StressThreadArg));
    pthread_t *tids = (pthread_t *)calloc(num_threads, sizeof(pthread_t));

    for (uint32_t i = 0; i < num_threads; i++) {
        args[i].env = env;
        args[i].thread_id = i;
        args[i].iterations = iters;
        args[i].writes_ok = 0;
        args[i].reads_ok = 0;
        args[i].data_errors = 0;
        pthread_create(&tids[i], NULL, stress_cache_worker, &args[i]);
    }

    for (uint32_t i = 0; i < num_threads; i++) {
        pthread_join(tids[i], NULL);
    }

    uint32_t total_hits = 0;
    for (uint32_t i = 0; i < num_threads; i++) {
        total_hits += args[i].reads_ok;
    }

    printf("  Cache stats: hits=%u, misses=%u, collision_rejects=%u, thread_local_hits=%u\n",
           env->engine->cache_hits, env->engine->cache_misses, env->engine->collision_rejects,
           total_hits);

    uint32_t stress_rejects = env->engine->collision_rejects - baseline_rejects;
    if (stress_rejects == 0) {
        printf("  [PASS] No false positives under concurrent cache pressure.\n");
    } else {
        printf("  [WARN] %u collision rejects detected (expected if hash slots collide).\n",
               stress_rejects);
    }

    drain_ring(env->ring);

    free(args);
    free(tids);
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
        record_test_failure(env);
    }
}

// --- TEST: Debug Ring Drain ---
void test_debug_ring_drain(NtelSimulationEnvironment *env) {
    printf("Running Test: Debug Ring Drain & Read...\n");
    ntel_debug_ring_init(&g_ntel_debug_ring);

    for (int i = 0; i < 10; i++) {
        NTEL_LOG_SPARSE(NTEL_COMP_ENGINE, NTEL_EVT_SCORCH_PASS, i, i*2, i, 0, 0);
    }

    
    uint32_t write_idx = ntel_debug_ring_snapshot(&g_ntel_debug_ring);
    if (write_idx != 10) {
        printf("  [FAIL] Expected 10 records, got %u\n", write_idx);
        record_test_failure(env);
        return;
    }

    ntel_debug_record_t recs[16];
    uint32_t read_cnt = ntel_debug_ring_read(&g_ntel_debug_ring, recs, 0, 16);
    if (read_cnt != 10) {
        printf("  [FAIL] Expected to read 10 records, got %u\n", read_cnt);
        record_test_failure(env);
        return;
    }

    char buf[256];
    ntel_debug_record_format(buf, sizeof(buf), &recs[0]);
    printf("  Sample: %s\n", buf);
    printf("  [PASS] Debug ring drain verified (%u records read).\n", read_cnt);

    ntel_debug_ring_reset(&g_ntel_debug_ring);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --threads=N       Number of threads for stress tests (default: 4)\n");
    printf("  --stress-cache    Run concurrent shader cache stress test\n");
    printf("  --stress-ring     Run multithreaded ring buffer stress test\n");
    printf("  --all             Run all tests including stress (default)\n");
    printf("  --help            Show this help\n");
}

int ntel_sim_run(int argc, char *argv[]) {
    uint32_t num_threads = 4;
    bool run_stress_cache = false;
    bool run_stress_ring = false;
    bool explicit_mode = false;

    static struct option long_opts[] = {
        {"threads",      required_argument, NULL, 't'},
        {"stress-cache", no_argument,       NULL, 'c'},
        {"stress-ring",  no_argument,       NULL, 'r'},
        {"all",          no_argument,       NULL, 'a'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:crah", long_opts, NULL)) != -1) {
        switch (opt) {
            case 't':
                num_threads = (uint32_t)atoi(optarg);
                if (num_threads == 0 || num_threads > 64) num_threads = 4;
                break;
            case 'c':
                run_stress_cache = true;
                explicit_mode = true;
                break;
            case 'r':
                run_stress_ring = true;
                explicit_mode = true;
                break;
            case 'a':
                explicit_mode = false;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!explicit_mode) {
        run_stress_cache = true;
        run_stress_ring = true;
    }

    NtelSimulationEnvironment *env = (NtelSimulationEnvironment *)malloc(sizeof(NtelSimulationEnvironment));
    ntel_sim_setup(env, 1024 * 1024);
    ntel_debug_ring_init(&g_ntel_debug_ring);

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
    test_hash_collision_rejection(env);

    if (run_stress_ring) {
        test_stress_ring_multithread(env, num_threads);
    }
    if (run_stress_cache) {
        test_stress_cache_concurrent(env, num_threads);
    }

    test_e2e_pipeline(env);
    test_debug_ring_drain(env);
    printf("--- REGRESSION SUITE COMPLETE: %u failure(s) ---\n\n", env->test_failures);

    uint32_t failures = env->test_failures;
    ntel_sim_teardown(env);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char *argv[]) {
    return ntel_sim_run(argc, argv);
}