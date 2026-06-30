#ifndef NTEL_TRANSLATION_ENGINE_H
#define NTEL_TRANSLATION_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#ifndef __APPLE__
#include <pthread.h>
#endif
#include "NtelSharedRing.h"

#define INTEL_GEN12_SIMD_WIDTH 32
#define NTEL_SHADER_CACHE_SIZE 256
#define NTEL_MAX_SHADER_BYTECODE (64 * 1024)

typedef struct {
    uint64_t kernel_gpu_va;
    uint32_t binding_table_ptr;
    uint32_t sampler_ptr;
    uint32_t slm_config;
    uint32_t eu_thread_count;
    uint32_t reserved;
    uint32_t padding;
} NtelIDD;

typedef struct {
    uint64_t hash;
    uint32_t verify_tag;
    uint32_t air_source_len;
    uint8_t *gen12_bytecode;
    uint32_t bytecode_size;
    bool valid;
} NtelShaderCacheEntry;

typedef struct {
    NtelRingContext *ring;
    void *isa_mapping_table;
    uint32_t active_pid;
    NtelShaderCacheEntry shader_cache[NTEL_SHADER_CACHE_SIZE];
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t collision_rejects;
#ifdef __APPLE__
    /* In kernel space, use IOLock or IORecursiveLock */
    void *cache_lock;
#else
    pthread_mutex_t cache_lock;
#endif
} NtelTranslationEngine;

bool ntel_engine_init(NtelTranslationEngine *engine, NtelRingContext *ring);
bool ntel_engine_process_command(NtelTranslationEngine *engine, const uint8_t *air_packet, uint32_t len);
void ntel_engine_cleanup(NtelTranslationEngine *engine);
void ntel_engine_scorch_pass(NtelTranslationEngine *engine);

uint32_t ntel_calculate_predicate_mask(uint32_t threads_per_group, uint32_t simd_width);

uint64_t ntel_shader_hash(const uint8_t *data, uint32_t len);
uint32_t ntel_shader_verify_tag(const uint8_t *data, uint32_t len);
bool ntel_shader_cache_lookup(NtelTranslationEngine *engine, uint64_t hash, uint32_t verify_tag,
                               uint32_t air_source_len, uint8_t **out_bytecode, uint32_t *out_size);
bool ntel_shader_cache_store(NtelTranslationEngine *engine, uint64_t hash, uint32_t verify_tag,
                              uint32_t air_source_len, const uint8_t *bytecode, uint32_t size);
void ntel_shader_cache_flush(NtelTranslationEngine *engine);
void ntel_shader_cache_release_bytecode(uint8_t *bytecode, uint32_t size);

#endif // NTEL_TRANSLATION_ENGINE_H
