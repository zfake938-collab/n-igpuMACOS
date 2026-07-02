// Baseline Metal Shader Corpus - Clear Screen
// Compiles to minimal AIR bytecode for dictionary extraction

#include <metal_stdlib>
using namespace metal;

// Vertex shader - clear screen with solid color
// Purpose: Isolate MOV and SEND opcodes
vertex float4 clear_vertex(uint vertex_id [[vertex_id]],
                            uint instance_id [[instance_id]]) {
    // Simple pass-through - MOV operations
    float4 pos = float4(0.0, 0.0, 0.0, 1.0);
    return pos;
}

// Fragment shader - solid red color
// Purpose: Isolate render target write (SEND)
fragment float4 clear_fragment() {
    // Return solid red - will use MOV + SEND
    return float4(1.0, 0.0, 0.0, 1.0);
}

// Compute shader - basic arithmetic
// Purpose: Isolate FADD, FMUL opcodes
kernel void math_kernel(device float* input [[buffer(0)]],
                       device float* output [[buffer(1)]],
                       uint gid [[thread_position_in_threadgroup]]) {
    float val = input[gid];
    // FADD: val + 2.0
    float result = val + 2.0;
    // FMUL: result * 3.0
    output[gid] = result * 3.0;
}

// Compute shader - threadgroup barrier
// Purpose: Isolate AIR_OP_BARRIER
kernel void barrier_kernel(threadgroup float* shared [[threadgroup(0)]],
                          uint tid [[thread_index_in_threadgroup]],
                          uint tg_size [[threadgroups_per_grid]]) {
    shared[tid] = float(tid);
    threadgroup_barrier(mem_flags::mem_read_write);
    // Read back after barrier
    float val = shared[(tid + 1) % tg_size];
}