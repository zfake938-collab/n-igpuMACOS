# Ntel: Architecture Deep Dive

This document provides the technical specifications, mathematical models, and structural layouts used by the Ntel framework. It is intended for systems engineers and researchers working on low-level graphics translation and kernel-mode drivers.

> **Note**: The identity spoofing path (NtelSpoofKext) is implemented and uses Intel Ice Lake Gen11 IDs to bind `AppleIntelICLLPGraphicsFramebuffer`. The translation engine, cache coherency protocol, and firmware injection described below are Phase 2 work-in-progress.

---

## 🧠 Core Design Philosophy

Ntel is built on the principle of **Minimal Intervention**. Instead of attempting to rewrite the macOS graphics stack, Ntel creates a high-fidelity "translation sandwich" between the driver and the hardware, minimizing the performance overhead inherent in cross-architecture ISA remapping.

---

## 🔄 Memory & Communication: The NtelSharedRing

The backbone of the Ntel communication pipeline is a lock-free, multi-producer single-consumer (MPSC) ring buffer designed for extreme throughput and VA-invariance.

### 1. VA-Invariant Addressing
To allow the kernel (Ring 0) and the DriverKit extension (User Space) to share the same physical memory without pointer corruption, the ring uses **byte-based offsets** rather than absolute memory addresses.

**Ring Header Layout (64-byte aligned):**
| Offset | Field | Description |
| :--- | :--- | :--- |
| `0x00` | `writeIdx` | Atomic producer offset (byte-based) |
| `0x04` | `readIdx` | Consumer offset (byte-based) |
| `0x08` | `capacityDW` | Total ring capacity in Double Words |
| `0x0C` | `reserved[13]` | Padding to ensure 64-byte cache-line alignment |

### 2. Cache Coherency Protocol
Because the Intel Xe architecture utilizes a non-snooping path for command submission, Ntel enforces a strict synchronization contract:
1.  **Host Write**: Command data is written to the ring.
2.  **Eviction**: An `_mm_clflush` is executed across the modified segment.
3.  **Barrier**: An `_mm_mfence` is issued to prevent compiler/CPU reordering.
4.  **Doorbell**: The hardware doorbell is rung only after the barrier is confirmed.

> **Implementation Status**: The current ring buffer uses `OSMemoryBarrier()` for ordering. The `_mm_clflush`/`_mm_mfence` cache-line eviction contract for non-snooping GPU visibility requires additional implementation.

---

## ⚡ ISA Translation: AIR $\rightarrow$ Gen12 (Phase 2)

The translation engine manages the conversion of Apple Intermediate Representation (AIR) to Intel Execution Unit (EU) bytecode.

> **Implementation Status**: `translate_air_to_gen12()` currently implements a stub that XORs input bytes with `0xA5`. The full 3-stage LUT pipeline (Parsing → Transmutation → Descriptor Generation) requires significant additional development.

### 1. The Command Pipeline (Planned)
The translation follows a three-stage pipeline:
1.  **Parsing**: Deconstructing the Metal/AIR command buffer.
2.  **Transmutation**: Mapping AIR opcodes to Gen12 ISA instructions via a high-speed Look-Up Table (LUT).
3.  **Descriptor Generation**: Constructing the `NtelIDD` (Interface Descriptor Data) to guide the Intel GPU hardware.

### 2. IDD Specification
Every compute pipeline dispatch generates a 32-byte `NtelIDD` descriptor:
*   **DW0/DW1**: 64-byte aligned Kernel GPU Virtual Address.
*   **DW2/DW3**: Transmuted Binding Table and Sampler pointers.
*   **DW4**: SLM (Shared Local Memory) configuration (Tiering & Barriers).
*   **DW5**: EU Thread Count and SIMD masking bits.

### 3. Occupancy & Predicate Masking
To handle the "Occupancy Cliff" (where Intel's static SLM tiers cause underutilization), Ntel calculates a hardware predicate mask for irregular thread grids:
$$\text{remainder} = \text{threadsPerGroup} \pmod{\text{simdWidth}}$$
$$\text{rightExecMask} = (1 \ll \text{remainder}) - 1$$

---

## 🛡️ Security & Stability

### 1. Context Isolation
To prevent "Context Bleed" (residual state leaking between different PIDs), the engine implements a **Sentinel Lifecycle**:
*   Upon PID mismatch, the `_activeContextID` triggers a **Scorch Pass**.
*   The engine zeros out all internal tracking caches and binding table pointers.
*   A hardware `PIPE_CONTROL` command is injected to flush all hardware state.

### 2. Deadlock Defense
Ntel uses a **3-Color Depth-First Search (DFS)** algorithm to perform cycle detection on the dependency graph before commands reach the hardware. If a cyclic dependency (A waits on B, B waits on A) is detected, the engine aborts the submission and triggers a `SYSLOG` alert.

---

## 📈 Performance Targets
*   **Translation Overhead**: $< 15\%$ CPU utilization for desktop compositor workloads.
*   **Latency**: $< 10\mu s$ ring submission overhead.
*   **Stability**: Zero kernel panics across 10,000 hours of continuous stress testing.
