# Phase 2 Implementation: Hardware Acceleration Roadmap

This document details the implementation progress and remaining work to bridge the gap between Phase 1 (PCIe identity spoofing) and full Metal graphics acceleration.

---

## 📊 Current Implementation Status

| Component | Status | Location | Notes |
|-----------|--------|----------|-------|
| Cache Coherency Contract | ✅ IMPLEMENTED | `src/NtelSharedRing.c:20-47` | CLFLUSH/MFENCE for non-snooping paths |
| Firmware Loading | ✅ DONE (Phase 1) | `src/NtelFirmwareInjector.c:30-136` | Blobs loaded to kernel memory |
| MMIO Upload Hooks | ✅ IMPLEMENTED | `src/NtelFirmwareInjector.c:241-344` | Stub implementations with timeout |
| AIR→Gen12 LUT Framework | ✅ IMPLEMENTED | `src/NtelTranslationEngine.c:22-275` | Function pointer-based 256-entry LUT |
| Core Translators | ✅ IMPLEMENTED | `src/NtelTranslationEngine.c:277-390` | fadd, fmul, add, mov, barrier, send |
| Cycle Detection (Deadlock Defense) | ✅ IMPLEMENTED | `src/NtelTranslationEngine.c:430-499` | 3-color DFS algorithm |
| NtelIDD Generation | ✅ IMPLEMENTED | `src/NtelTranslationEngine.c:401-410` | 32-byte descriptor header |

---

## 🔧 Step 1: Cache Coherency Contract (COMPLETE)

**File**: `src/NtelSharedRing.c`

### Implementation
```c
// Lines 20-47: Hardware cache eviction functions
static inline void ntel_cache_evict(const void *addr, uint32_t len) {
    // macOS: __builtin_ia32_clflush, Linux: _mm_clflush
    for (ptr < end; ptr += 64) {
        _mm_clflush(ptr);
    }
}

static inline void ntel_cache_barrier(void) {
    // macOS: __builtin_ia32_mfence, Linux: _mm_mfence
}
```

### Verification
- Tests pass with cache coherency validation
- Stress tests confirm no data corruption under thread contention

---

## 🔧 Step 2: MMIO Firmware Upload (IMPLEMENTED - STUB)

**File**: `src/NtelFirmwareInjector.c:241-344`

### API Added
```c
NtelFirmwareResult ntel_fw_map_mmio(NtelFirmwareContext *ctx, IOPCIDevice *pci_device);
NtelFirmwareResult ntel_fw_upload_guc_to_hw(NtelFirmwareContext *ctx);
NtelFirmwareResult ntel_fw_upload_huc_to_hw(NtelFirmwareContext *ctx);
```

### Remaining Work
- Replace stub MMIO mapping with actual `IOPCIDevice::mapMemory()` call
- Add proper register validation before writes
- Handle hardware-specific MMIO offsets for 1235U (0x46A8)

---

## 🔧 Step 3: AIR to Gen12 Translation (IMPLEMENTED - LUT FRAMEWORK)

**File**: `src/NtelTranslationEngine.c`

### Implemented Structure
```c
typedef struct {
    uint32_t dword0;  // Opcode, execution size, access mode
    uint32_t dword1;  // Destination register
    uint32_t dword2;  // Source 0 register
    uint32_t dword3;  // Source 1 / immediate / control
} NtelGen12Instruction;

typedef NtelGen12Instruction (*NtelOpcodeTranslator)(const uint8_t *payload, uint32_t *offset);

typedef struct {
    const char *mnemonic;
    NtelOpcodeTranslator translate_fn;
    uint8_t payload_size;
} NtelOpcodeMap;
```

### Core Translator Functions
- `translate_fadd()` - Float add → Gen12 ADD
- `translate_fmul()` - Float multiply → Gen12 MUL
- `translate_add()` - Integer add → Gen12 ADD
- `translate_mov()` - Move → Gen12 MOV
- `translate_barrier()` - Sync.bar for thread synchronization
- `translate_send()` - Sampler access via SEND instruction

### LUT Initialization
- `ntel_init_opcode_lut()` - Populates 256-entry function pointer table
- Called once during `ntel_engine_init()`

---

## 🔧 Step 4: Deadlock Defense (IMPLEMENTED)

**File**: `src/NtelTranslationEngine.c:310-349`

### Implementation
```c
typedef enum {
    NTEL_CYCLE_WHITE = 0, // Unvisited
    NTEL_CYCLE_GRAY  = 1, // In progress
    NTEL_CYCLE_BLACK = 2  // Completed
} NtelCycleState;

// 3-Color DFS algorithm detects cyclic dependencies
static bool detect_cycle_dfs(NtelCycleState *states, uint32_t node_count,
                              const uint32_t **deps, const uint32_t *dep_counts);
```

---

## 📋 Remaining Critical Work

### Task A: Real AIR Opcode Extraction
The framework needs actual AIR opcodes from Metal.framework. 

**Baseline Shader Corpus Created:** `shaders/clear_screen.metal`
- `clear_vertex` - MOV opcode isolation
- `clear_fragment` - SEND opcode isolation  
- `math_kernel` - FADD/FMUL opcode isolation
- `barrier_kernel` - Threadgroup barrier extraction

**Extraction Workflow:**
```bash
# Compile and extract opcodes
python3 tools/extract_air_opcodes.py shaders/clear_screen.metal build

# Analyze output
llvm-bcanalyzer -dump build/clear_screen.air
```

### Task B: Intel GuC/HuC Regsiter Documentation
MMIO offsets need validation for 1235U:
- `INTEL_GUC_STATUS_REG_OFFSET` (currently 0x4000)
- `INTEL_GUC_LOAD_REG_OFFSET` (currently 0x4800)
- Actual values from Intel documentation or i915 DRM kernel driver

### Task C: Framebuffer Integration
Connect translation output to `AppleIntelICLLPGraphicsFramebuffer`:
- Hook into `AppleIntelFramebufferUserClient`
- Replace command submission with translated bytecode
- Validate GPU execution via render tests

---

## 🛠️ Testing Strategy

### Simulation Tests (Current)
```bash
cd NtelSpoofKext && make test
# All 12 tests pass with cache coherency enabled
```

### Hardware Debugging (Required)
1. Two-machine KDP setup:
   ```bash
   # Target (boot-args):
   nvram boot-args="kdp_match_name=ntel debug=0x144"
   
   # Host:
   lldb -k kernel.development
   (lldb) kdp-remote <target-ip>
   ```

2. Enable verbose logging:
   ```bash
   nvram boot-args="amfi=0x80 ntel_debug=4"
   ```

---

## 🔗 References

- Intel Xe-LP Microarchitecture: https://01.org/linuxgraphics/documentation
- AIR Specification: Apple Metal Private Headers (reverse engineered)
- IOKit MMIO: `IOPCIDevice::mapMemory()` in Xcode KDK
- PIPE_CONTROL command: Intel Graphics Programmer's Reference Manual