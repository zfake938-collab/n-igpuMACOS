#include "NtelTranslationEngine.h"
#include "NtelDebugRing.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#define NTEL_CACHE_LOCK(e)   do { if ((e)->cache_lock) IOLockLock((IOLock*)(e)->cache_lock); } while(0)
#define NTEL_CACHE_UNLOCK(e) do { if ((e)->cache_lock) IOLockUnlock((IOLock*)(e)->cache_lock); } while(0)
#define NTEL_ALLOC(s)        IOMalloc(s)
#define NTEL_FREE(p, s)      IOFree((p), (s))
#else
#define NTEL_CACHE_LOCK(e)   pthread_mutex_lock(&(e)->cache_lock)
#define NTEL_CACHE_UNLOCK(e) pthread_mutex_unlock(&(e)->cache_lock)
#define NTEL_ALLOC(s)        malloc(s)
#define NTEL_FREE(p, s)      free(p)
#endif

// Forward declarations - translator functions
static NtelGen12Instruction translate_fadd(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_fmul(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_add(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_mov(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_barrier(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_send(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_unknown(const uint8_t *payload, uint32_t *offset);
static void ntel_init_opcode_lut(NtelOpcodeMap *lut);

static const uint32_t crc32_table[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D09,0x90BF1D9F,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
    0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
    0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
    0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
    0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
    0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA8670955,0x31684D8A,0x4C6F7B1C,
    0xD208EF04,0xA50FDF92,0x3C068E28,0x4B01BE9E,0xD5656B3D,0xA2625BAB,0x3B6B0A11,0x4C6C3A87,
    0xDCD00B16,0xABD73B80,0x32DE6A3A,0x45D95AAC,0xDBBD3F0F,0xACBA4F99,0x35B31E23,0x42B42EB5,
    0xCB6E0164,0xBC6931F2,0x25606048,0x526750DE,0xCC03C57D,0xBB04F5EB,0x220DA451,0x550A94C7,
    0xC5B58956,0xB2B2B9C0,0x2BBBE87A,0x5CBCE8EC,0xC2D8AD4F,0xB5DF9DD9,0x2CD6CC63,0x5BD1FCF5,
    0x94D9810E,0xE3DEB198,0x7AD7E022,0x0DD0D0B4,0x93B4B517,0xE4B38581,0x7DBAD43B,0x0ABDE4AD,
    0x9A02F93C,0xED05C9AA,0x740C9810,0x030BA886,0x9D6F3D25,0xEA680DB3,0x73615C09,0x04666C9F,
    0xB4D61564,0xC3D125F2,0x5AD87448,0x2DDF44DE,0xB3BB217D,0xC4BC11EB,0x5DB54051,0x2AB270C7,
    0xBA0D6D56,0xCD0A5DC0,0x54030C7A,0x23043CEC,0xBD60594F,0xCA6769D9,0x536E3863,0x246908F5,
    0xA7610908,0xD066399E,0x496F6824,0x3E6858B2,0xA00C3D11,0xD70B0D87,0x4E025C3D,0x39056CAB,
    0xA9BA713A,0xDEBD41AC,0x47B41016,0x30B32080,0xAED7B523,0xD9D085B5,0x40D9D40F,0x37DEE499
};

uint64_t ntel_shader_hash(const uint8_t *data, uint32_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint32_t ntel_shader_verify_tag(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool ntel_engine_init(NtelTranslationEngine *engine, NtelRingContext *ring) {
    if (!engine || !ring) return false;

    engine->ring = ring;
    engine->active_pid = 0;
    ntel_init_opcode_lut(engine->isa_mapping_table);
    engine->cache_hits = 0;
    engine->cache_misses = 0;
    engine->collision_rejects = 0;

    memset(engine->shader_cache, 0, sizeof(engine->shader_cache));

#if defined(__APPLE__) && !defined(NTEL_USERMODE)
    engine->cache_lock = (void *)IOLockAlloc();
    if (!engine->cache_lock) {
        return false;
    }
#else
    if (pthread_mutex_init(&engine->cache_lock, NULL) != 0) {
        return false;
    }
#endif

    return true;
}

uint32_t ntel_calculate_predicate_mask(uint32_t threads_per_group, uint32_t simd_width) {
    if (simd_width == 0) return 0;

    /* Clamp simd_width to 32: shifts of 32 or more bits on a uint32_t are
       undefined behaviour in C. Any SIMD width > 32 is treated as "full warp". */
    if (simd_width > 32) return 0xFFFFFFFF;

    uint32_t remainder = threads_per_group % simd_width;
    if (remainder == 0) return 0xFFFFFFFF;

    /* remainder is in [1, simd_width-1] ⊆ [1, 31] here — shift is always safe. */
    return (1u << remainder) - 1;
}

bool ntel_shader_cache_lookup(NtelTranslationEngine *engine, uint64_t hash, uint32_t verify_tag,
                               uint32_t air_source_len, uint8_t **out_bytecode, uint32_t *out_size) {
    if (!engine || !out_bytecode || !out_size) return false;

    *out_bytecode = NULL;
    *out_size = 0;

    NTEL_CACHE_LOCK(engine);

    uint32_t index = (uint32_t)(hash % NTEL_SHADER_CACHE_SIZE);
    NtelShaderCacheEntry *entry = &engine->shader_cache[index];

    if (entry->valid && entry->hash == hash) {
        if (entry->verify_tag != verify_tag || entry->air_source_len != air_source_len) {
            engine->collision_rejects++;
            engine->cache_misses++;
            NTEL_LOG_HOT(NTEL_COMP_CACHE, NTEL_EVT_CACHE_COLLISION_MISMATCH,
                         0, index, 0, entry->air_source_len, -1);
            NTEL_CACHE_UNLOCK(engine);
            return false;
        }

        if (!entry->gen12_bytecode || entry->bytecode_size == 0) {
            engine->cache_misses++;
            NTEL_CACHE_UNLOCK(engine);
            return false;
        }

        uint8_t *copy = (uint8_t *)NTEL_ALLOC(entry->bytecode_size);
        if (!copy) {
            engine->cache_misses++;
            NTEL_CACHE_UNLOCK(engine);
            return false;
        }

        memcpy(copy, entry->gen12_bytecode, entry->bytecode_size);
        *out_bytecode = copy;
        *out_size = entry->bytecode_size;
        engine->cache_hits++;
        NTEL_CACHE_UNLOCK(engine);
        return true;
    }

    engine->cache_misses++;
    NTEL_CACHE_UNLOCK(engine);
    return false;
}

bool ntel_shader_cache_store(NtelTranslationEngine *engine, uint64_t hash, uint32_t verify_tag,
                              uint32_t air_source_len, const uint8_t *bytecode, uint32_t size) {
    if (!engine || !bytecode || size == 0 || size > NTEL_MAX_SHADER_BYTECODE) return false;

    NTEL_CACHE_LOCK(engine);

    uint32_t index = (uint32_t)(hash % NTEL_SHADER_CACHE_SIZE);
    NtelShaderCacheEntry *entry = &engine->shader_cache[index];

    if (entry->valid && entry->gen12_bytecode) {
        NTEL_FREE(entry->gen12_bytecode, entry->bytecode_size);
    }
    entry->gen12_bytecode = NULL;
    entry->bytecode_size = 0;
    entry->valid = false;

    entry->gen12_bytecode = (uint8_t *)NTEL_ALLOC(size);
    if (!entry->gen12_bytecode) {
        NTEL_CACHE_UNLOCK(engine);
        return false;
    }

    memcpy(entry->gen12_bytecode, bytecode, size);
    entry->bytecode_size = size;
    entry->hash = hash;
    entry->verify_tag = verify_tag;
    entry->air_source_len = air_source_len;
    entry->valid = true;

    NTEL_CACHE_UNLOCK(engine);
    return true;
}

void ntel_shader_cache_flush(NtelTranslationEngine *engine) {
    if (!engine) return;

    NTEL_CACHE_LOCK(engine);

    for (uint32_t i = 0; i < NTEL_SHADER_CACHE_SIZE; i++) {
        NtelShaderCacheEntry *entry = &engine->shader_cache[i];
        if (entry->valid && entry->gen12_bytecode) {
            NTEL_FREE(entry->gen12_bytecode, entry->bytecode_size);
            entry->gen12_bytecode = NULL;
        }
        entry->valid = false;
        entry->hash = 0;
        entry->verify_tag = 0;
        entry->air_source_len = 0;
        entry->bytecode_size = 0;
    }

    engine->cache_hits = 0;
    engine->cache_misses = 0;
    engine->collision_rejects = 0;

    NTEL_CACHE_UNLOCK(engine);
}

void ntel_shader_cache_release_bytecode(uint8_t *bytecode, uint32_t size) {
    if (!bytecode || size == 0) return;
    NTEL_FREE(bytecode, size);
}

// Forward declarations for translator functions
static NtelGen12Instruction translate_fadd(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_fmul(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_add(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_mov(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_barrier(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_send(const uint8_t *payload, uint32_t *offset);
static NtelGen12Instruction translate_unknown(const uint8_t *payload, uint32_t *offset);

// Initialize the LUT with function pointers - called once at engine init
static void ntel_init_opcode_lut(NtelOpcodeMap *lut) {
    // Clear all entries
    for (int i = 0; i < 256; i++) {
        lut[i].mnemonic = "unknown";
        lut[i].translate_fn = translate_unknown;
        lut[i].payload_size = 1;
    }
    
    // ALU Translations
    lut[0x03].mnemonic = "fadd";  // AIR_OP_FADD
    lut[0x03].translate_fn = translate_fadd;
    lut[0x03].payload_size = 4;
    
    lut[0x04].mnemonic = "fmul";  // AIR_OP_FMUL
    lut[0x04].translate_fn = translate_fmul;
    lut[0x04].payload_size = 4;
    
    lut[0x01].mnemonic = "add";   // AIR_OP_ADD
    lut[0x01].translate_fn = translate_add;
    lut[0x01].payload_size = 4;
    
    lut[0x00].mnemonic = "mov";   // AIR_OP_MOV
    lut[0x00].translate_fn = translate_mov;
    lut[0x00].payload_size = 3;
    
    // Memory/Sampler (send message)
    lut[0x20].mnemonic = "send";  // AIR_OP_SAMPLE
    lut[0x20].translate_fn = translate_send;
    lut[0x20].payload_size = 4;
    
    // Control flow / synchronization
    lut[0x30].mnemonic = "sync.bar";  // AIR_OP_BARRIER
    lut[0x30].translate_fn = translate_barrier;
    lut[0x30].payload_size = 2;
}

// Simple virtual-to-physical GRF mapping (placeholder)
static inline uint32_t map_virtual_to_grf(uint8_t virt_reg) {
    return virt_reg & 0x3F;  // Gen12 has 64 GRFs (0-63)
}

// Translator implementations
static NtelGen12Instruction translate_fadd(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    // Extract register operands
    uint8_t dest_reg = payload[*offset + 1];
    uint8_t src0_reg = payload[*offset + 2];
    uint8_t src1_reg = payload[*offset + 3];
    
    // Gen12 ADD opcode for float: 0x40 << 24 (opcode in bits 24-31)
    inst.dword0 = (0x40 << 24) | 0x1;  // ExecSize=1 (SIMD1)
    inst.dword1 = map_virtual_to_grf(dest_reg) << 4;  // Destination in bits 4-9
    inst.dword2 = map_virtual_to_grf(src0_reg) << 4;
    inst.dword3 = map_virtual_to_grf(src1_reg) << 4;
    
    *offset += 4;
    return inst;
}

static NtelGen12Instruction translate_fmul(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    uint8_t dest_reg = payload[*offset + 1];
    uint8_t src0_reg = payload[*offset + 2];
    uint8_t src1_reg = payload[*offset + 3];
    
    // Gen12 MUL opcode: 0x46
    inst.dword0 = (0x46 << 24) | 0x1;
    inst.dword1 = map_virtual_to_grf(dest_reg) << 4;
    inst.dword2 = map_virtual_to_grf(src0_reg) << 4;
    inst.dword3 = map_virtual_to_grf(src1_reg) << 4;
    
    *offset += 4;
    return inst;
}

static NtelGen12Instruction translate_add(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    uint8_t dest_reg = payload[*offset + 1];
    uint8_t src0_reg = payload[*offset + 2];
    uint8_t src1_reg = payload[*offset + 3];
    
    // Gen12 ADD integer opcode: 0x40
    inst.dword0 = (0x40 << 24) | 0x1;
    inst.dword1 = map_virtual_to_grf(dest_reg) << 4;
    inst.dword2 = map_virtual_to_grf(src0_reg) << 4;
    inst.dword3 = map_virtual_to_grf(src1_reg) << 4;
    
    *offset += 4;
    return inst;
}

static NtelGen12Instruction translate_mov(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    uint8_t dest_reg = payload[*offset + 1];
    uint8_t src0_reg = payload[*offset + 2];
    
    // Gen12 MOV opcode: 0x50
    inst.dword0 = (0x50 << 24) | 0x1;
    inst.dword1 = map_virtual_to_grf(dest_reg) << 4;
    inst.dword2 = map_virtual_to_grf(src0_reg) << 4;
    
    *offset += 3;
    return inst;
}

static NtelGen12Instruction translate_barrier(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    // Gen12 SYNC.BAR format: 0x70 << 24
    inst.dword0 = 0x70 << 24;
    inst.dword3 = 0x0F;  // Barrier control bits
    
    *offset += 2;
    return inst;
}

static NtelGen12Instruction translate_send(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (!payload || !offset) return inst;
    
    // Gen12 SEND for sampler: 0x42 << 24
    inst.dword0 = 0x42 << 24;
    inst.dword3 = payload[*offset + 3];  // Binding table index
    
    *offset += 4;
    return inst;
}

static NtelGen12Instruction translate_unknown(const uint8_t *payload, uint32_t *offset) {
    NtelGen12Instruction inst = {0};
    
    if (offset && payload) {
        /* Log unknown AIR opcode for debugging, but avoid unlimited spam. */
        static uint32_t unknown_count = 0;
        if (unknown_count < 1000) {
            uint8_t op = payload[*offset];
            NTEL_LOG_SPARSE(NTEL_COMP_ENGINE, NTEL_EVT_TRANSLATION_FAILED,
                           0, *offset, 0, unknown_count, (int32_t)op);
            unknown_count++;
        }
        (*offset)++;
    }

    /* Emit a safe no-op instruction so the pipeline remains aligned. */
    inst.dword0 = 0x00000000;
    return inst;
}

static void ntel_build_idd_header(NtelIDD *idd) {
    if (!idd) return;
    idd->kernel_gpu_va = 0x0000000000100000ULL;
    idd->binding_table_ptr = 0x00002000;
    idd->sampler_ptr = 0x00003000;
    idd->slm_config = 0x00001000;
    idd->eu_thread_count = 32;
    idd->reserved = 0;
    idd->padding = 0;
}

static bool translate_air_to_gen12(const uint8_t *air_packet, uint32_t air_len,
                                     uint8_t **out_gen12, uint32_t *out_gen12_len) {
    if (!air_packet || air_len == 0 || !out_gen12 || !out_gen12_len) return false;
    if (air_len > (NTEL_MAX_SHADER_BYTECODE / 4)) return false;  // Max ~16K instructions

    uint32_t max_instructions = air_len;
    uint32_t gen12_size = 32 + (max_instructions * sizeof(NtelGen12Instruction));
    uint8_t *gen12 = (uint8_t *)NTEL_ALLOC(gen12_size);
    if (!gen12) return false;

    memset(gen12, 0, gen12_size);
    ntel_build_idd_header((NtelIDD *)gen12);

    uint32_t offset = 0;
    uint32_t inst_count = 0;
    NtelGen12Instruction inst;

    static NtelOpcodeMap local_lut[256];
    static bool lut_initialized = false;
    if (!lut_initialized) {
        ntel_init_opcode_lut(local_lut);
        lut_initialized = true;
    }

    while (offset < air_len && inst_count < max_instructions) {
        uint8_t air_op = air_packet[offset];
        const NtelOpcodeMap *entry = &local_lut[air_op];
        uint32_t next_offset = offset + entry->payload_size;

        if (entry->translate_fn && next_offset <= air_len) {
            inst = entry->translate_fn(air_packet, &offset);
        } else {
            inst = translate_unknown(air_packet, &offset);
        }

        memcpy(gen12 + 32 + (inst_count * sizeof(NtelGen12Instruction)), &inst,
               sizeof(NtelGen12Instruction));
        inst_count++;
    }

    *out_gen12 = gen12;
    *out_gen12_len = 32 + (inst_count * sizeof(NtelGen12Instruction));
    return true;
}

// 3-Color DFS Cycle Detection for Deadlock Defense (Phase 2)
// Used for dependency graph cycle detection before command submission
// Currently integrated into ntel_engine_process_command when dependency tracking is added
__attribute__((unused))
static bool detect_cycle_dfs(NtelCycleState *states, uint32_t node_count,
                              const uint32_t **deps, const uint32_t *dep_counts) {
    if (!states || !deps || !dep_counts) return false;

    bool has_cycle = false;
    
    for (uint32_t start = 0; start < node_count && !has_cycle; start++) {
        if (states[start] != NTEL_CYCLE_WHITE) continue;
        
        uint32_t stack[256];
        uint32_t sp = 0;
        stack[sp++] = start;
        states[start] = NTEL_CYCLE_GRAY;
        
        while (sp > 0 && !has_cycle) {
            uint32_t current = stack[sp - 1];
            bool pushed = false;
            
            for (uint32_t d = 0; d < dep_counts[current]; d++) {
                uint32_t dep = deps[current][d];
                if (dep >= node_count) continue;
                
                if (states[dep] == NTEL_CYCLE_GRAY) {
                    has_cycle = true;
                    break;
                }
                if (states[dep] == NTEL_CYCLE_WHITE) {
                    states[dep] = NTEL_CYCLE_GRAY;
                    stack[sp++] = dep;
                    pushed = true;
                    break;
                }
            }
            
            if (!pushed) {
                states[current] = NTEL_CYCLE_BLACK;
                sp--;
            }
        }
    }
    
    return has_cycle;
}

void ntel_engine_scorch_pass(NtelTranslationEngine *engine) {
    if (!engine) return;

    ntel_shader_cache_flush(engine);

    // Note: isa_mapping_table is now an inline array, no dynamic clearing needed
    // The LUT is initialized once at startup and persists

    NTEL_LOG_HOT(NTEL_COMP_ENGINE, NTEL_EVT_SCORCH_PASS,
                 0, 0, 0, engine->active_pid, 0);
}

bool ntel_engine_process_command(NtelTranslationEngine *engine, const uint8_t *air_packet, uint32_t len) {
    if (!engine || !engine->ring || !air_packet || len == 0) {
        return false;
    }

    uint32_t current_pid = (uint32_t)getpid();
#ifdef __APPLE__
    /* macOS usermode uses getpid(); kernel build should use proc_selfpid(). */
#endif

    if (engine->active_pid != 0 && engine->active_pid != current_pid) {
        NTEL_LOG_HOT(NTEL_COMP_ENGINE, NTEL_EVT_PID_MISMATCH,
                     0, 0, 0, current_pid, (int32_t)engine->active_pid);
        ntel_engine_scorch_pass(engine);
    }
    engine->active_pid = current_pid;

    uint64_t hash = ntel_shader_hash(air_packet, len);
    uint32_t vtag = ntel_shader_verify_tag(air_packet, len);

    uint8_t *cached_bytecode = NULL;
    uint32_t cached_size = 0;

    if (ntel_shader_cache_lookup(engine, hash, vtag, len, &cached_bytecode, &cached_size)) {
        bool result = ntel_ring_try_write(engine->ring, cached_bytecode, cached_size);
        ntel_shader_cache_release_bytecode(cached_bytecode, cached_size);
        return result;
    }

    uint8_t *gen12_bytecode = NULL;
    uint32_t gen12_size = 0;

    if (!translate_air_to_gen12(air_packet, len, &gen12_bytecode, &gen12_size)) {
        NTEL_LOG_HOT(NTEL_COMP_ENGINE, NTEL_EVT_TRANSLATION_FAILED,
                     0, 0, 0, engine->active_pid, -2);
        return false;
    }

    ntel_shader_cache_store(engine, hash, vtag, len, gen12_bytecode, gen12_size);

    bool result = ntel_ring_try_write(engine->ring, gen12_bytecode, gen12_size);

    ntel_shader_cache_release_bytecode(gen12_bytecode, gen12_size);

    return result;
}

void ntel_engine_cleanup(NtelTranslationEngine *engine) {
    if (engine) {
        ntel_shader_cache_flush(engine);
#if defined(__APPLE__) && !defined(NTEL_USERMODE)
        if (engine->cache_lock) IOLockFree((IOLock *)engine->cache_lock);
#else
        pthread_mutex_destroy(&engine->cache_lock);
#endif
        engine->ring = NULL;
    }
}
