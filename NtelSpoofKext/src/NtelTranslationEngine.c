#include "NtelTranslationEngine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

uint64_t ntel_shader_hash(const uint8_t *data, uint32_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

bool ntel_engine_init(NtelTranslationEngine *engine, NtelRingContext *ring) {
    if (!engine || !ring) return false;

    engine->ring = ring;
    engine->active_pid = 0;
    engine->isa_mapping_table = NULL;
    engine->cache_hits = 0;
    engine->cache_misses = 0;

    memset(engine->shader_cache, 0, sizeof(engine->shader_cache));

    return true;
}

uint32_t ntel_calculate_predicate_mask(uint32_t threads_per_group, uint32_t simd_width) {
    if (simd_width == 0) return 0;

    uint32_t remainder = threads_per_group % simd_width;
    if (remainder == 0) return 0xFFFFFFFF;

    return (1u << remainder) - 1;
}

bool ntel_shader_cache_lookup(NtelTranslationEngine *engine, uint64_t hash,
                               uint8_t **out_bytecode, uint32_t *out_size) {
    if (!engine || !out_bytecode || !out_size) return false;

    uint32_t index = (uint32_t)(hash % NTEL_SHADER_CACHE_SIZE);
    NtelShaderCacheEntry *entry = &engine->shader_cache[index];

    if (entry->valid && entry->hash == hash) {
        *out_bytecode = entry->gen12_bytecode;
        *out_size = entry->bytecode_size;
        engine->cache_hits++;
        return true;
    }

    engine->cache_misses++;
    return false;
}

bool ntel_shader_cache_store(NtelTranslationEngine *engine, uint64_t hash,
                              const uint8_t *bytecode, uint32_t size) {
    if (!engine || !bytecode || size == 0 || size > NTEL_MAX_SHADER_BYTECODE) return false;

    uint32_t index = (uint32_t)(hash % NTEL_SHADER_CACHE_SIZE);
    NtelShaderCacheEntry *entry = &engine->shader_cache[index];

    if (entry->valid && entry->gen12_bytecode) {
        free(entry->gen12_bytecode);
    }

    entry->gen12_bytecode = (uint8_t *)malloc(size);
    if (!entry->gen12_bytecode) return false;

    memcpy(entry->gen12_bytecode, bytecode, size);
    entry->bytecode_size = size;
    entry->hash = hash;
    entry->valid = true;

    return true;
}

void ntel_shader_cache_flush(NtelTranslationEngine *engine) {
    if (!engine) return;

    for (uint32_t i = 0; i < NTEL_SHADER_CACHE_SIZE; i++) {
        NtelShaderCacheEntry *entry = &engine->shader_cache[i];
        if (entry->valid && entry->gen12_bytecode) {
            free(entry->gen12_bytecode);
            entry->gen12_bytecode = NULL;
        }
        entry->valid = false;
        entry->hash = 0;
        entry->bytecode_size = 0;
    }

    engine->cache_hits = 0;
    engine->cache_misses = 0;
}

static bool translate_air_to_gen12(const uint8_t *air_packet, uint32_t air_len,
                                    uint8_t **out_gen12, uint32_t *out_gen12_len) {
    if (!air_packet || air_len == 0 || !out_gen12 || !out_gen12_len) return false;

    uint32_t gen12_size = air_len * 2;
    uint8_t *gen12 = (uint8_t *)malloc(gen12_size);
    if (!gen12) return false;

    for (uint32_t i = 0; i < air_len; i++) {
        gen12[i * 2] = air_packet[i] ^ 0xA5;
        gen12[i * 2 + 1] = air_packet[i] >> 1;
    }

    *out_gen12 = gen12;
    *out_gen12_len = gen12_size;
    return true;
}

void ntel_engine_scorch_pass(NtelTranslationEngine *engine) {
    if (!engine) return;

    ntel_shader_cache_flush(engine);

    if (engine->isa_mapping_table) {
        memset(engine->isa_mapping_table, 0, 1024);
    }

    printf("[ENGINE] Scorch Pass: Flushed shader cache and hardware state\n");
}

bool ntel_engine_process_command(NtelTranslationEngine *engine, const uint8_t *air_packet, uint32_t len) {
    if (!engine || !engine->ring || !air_packet || len == 0) {
        return false;
    }

    uint32_t current_pid = 1234;

    if (engine->active_pid != 0 && engine->active_pid != current_pid) {
        printf("[ENGINE] PID Mismatch (%u -> %u). Triggering Scorch Pass...\n",
               engine->active_pid, current_pid);
        ntel_engine_scorch_pass(engine);
    }
    engine->active_pid = current_pid;

    uint64_t hash = ntel_shader_hash(air_packet, len);

    uint8_t *cached_bytecode = NULL;
    uint32_t cached_size = 0;

    if (ntel_shader_cache_lookup(engine, hash, &cached_bytecode, &cached_size)) {
        printf("[ENGINE] Shader cache HIT (hash=0x%llx)\n", (unsigned long long)hash);
        return ntel_ring_try_write(engine->ring, cached_bytecode, cached_size);
    }

    printf("[ENGINE] Shader cache MISS (hash=0x%llx), translating...\n", (unsigned long long)hash);

    uint8_t *gen12_bytecode = NULL;
    uint32_t gen12_size = 0;

    if (!translate_air_to_gen12(air_packet, len, &gen12_bytecode, &gen12_size)) {
        printf("[ENGINE] Translation failed!\n");
        return false;
    }

    ntel_shader_cache_store(engine, hash, gen12_bytecode, gen12_size);

    bool result = ntel_ring_try_write(engine->ring, gen12_bytecode, gen12_size);

    free(gen12_bytecode);

    return result;
}

void ntel_engine_cleanup(NtelTranslationEngine *engine) {
    if (engine) {
        ntel_shader_cache_flush(engine);
        engine->isa_mapping_table = NULL;
        engine->ring = NULL;
    }
}
