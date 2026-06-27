#ifndef NTEL_TRANSLATION_ENGINE_H
#define NTEL_TRANSLATION_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "NtelSharedRing.h"

/**
 * @brief INTERFACE_DESCRIPTOR_DATA (IDD) Specification
 * Per EVP Section 3, defines the 32-byte descriptor for compute pipelines.
 */
typedef struct {
    uint64_t kernel_gpu_va;      // DW0/DW1: 64-byte aligned GPU VA
    uint32_t binding_table_ptr;  // DW2: Transmuted Binding Table pointer
    uint32_t sampler_ptr;        // DW3: Transmuted Sampler pointer
    uint32_t slm_config;         // DW4: SLM Tier bits 14:10, Barrier Enable bit 8
    uint32_t eu_thread_count;    // DW5: EU Thread Count bits 23:16
    uint32_t reserved;           // DW6: Padding
    uint32_t padding;            // DW7: Padding
} NtelIDD;

/**
 * @brief TranslationEngine Context
 * Manages the state of the AIR-to-Gen12 ISA translation layer.
 */
typedef struct {
    NtelRingContext *ring;
    // In a real implementation, this would hold pointers to the 
    // translation tables and the ISA mapping LUT (Look-Up Table).
    void *isa_mapping_table;
    uint32_t active_pid;
} NtelTranslationEngine;

// Core API
bool ntel_engine_init(NtelTranslationEngine *engine, NtelRingContext *ring);
bool ntel_engine_process_command(NtelTranslationEngine *engine, const uint8_t *air_packet, uint32_t len);
void ntel_engine_cleanup(NtelTranslationEngine *engine);

// ISA Specific Utilities
uint32_t ntel_calculate_predicate_mask(uint32_t threads_per_group, uint32_t simd_width);

#endif // NTEL_TRANSLATION_ENGINE_H
