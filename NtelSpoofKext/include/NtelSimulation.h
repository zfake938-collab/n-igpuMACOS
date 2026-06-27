#ifndef NTEL_SIMULATION_H
#define NTEL_SIMULATION_H

#include <stdint.h>
#include <stdbool.h>
#include "NtelSharedRing.h"
#include "NtelTranslationEngine.h"

/**
 * @brief Mocked hardware/kernel environment for deterministic testing.
 */
typedef struct {
    uint8_t *vram_mock;
    uint32_t vram_size;
    uint32_t gpu_clock_cycles;
    bool hardware_hang_detected;
} NtelMockHardware;

typedef struct {
    NtelMockHardware *hw;
    NtelRingContext *ring;
    NtelTranslationEngine *engine;
    uint32_t frame_count;
    bool watchdog_triggered;
} NtelSimulationEnvironment;

// Test Suite API
void ntel_sim_setup(NtelSimulationEnvironment *env, uint32_t vram_size);
void ntel_sim_teardown(NtelSimulationEnvironment *env);

// Structural Boundary Tests
void test_cache_line_tearing(NtelSimulationEnvironment *env);
void test_simd_remainder_occupancy(NtelSimulationEnvironment *env);
void test_asynchronous_watchdog(NtelSimulationEnvironment *env);
void test_ring_wraparound(NtelSimulationEnvironment *env);

// Environmental Stress Tests
void test_monkey_brain_fuzzer(NtelSimulationEnvironment *env);
void test_thread_riot(NtelSimulationEnvironment *env);
void test_chokehold_exhaustion(NtelSimulationEnvironment *env);
void test_context_bleed(NtelSimulationEnvironment *env);

// End-to-End
void test_e2e_pipeline(NtelSimulationEnvironment *env);

#endif // NTEL_SIMULATION_H
