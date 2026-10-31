// Shared declarations for the GpuScene culling passes (cull_scan / cull_blocks / cull_scatter).
// Visible instances are compacted with an exclusive prefix sum over a group-sorted order, so
// the visible list is deterministic and matches the CPU path's draw order exactly.
#ifndef ZODAR_CULL_COMMON_GLSL
#define ZODAR_CULL_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#define ZODAR_CULL_GROUP_SIZE 256

struct CullData {
    vec4 boundsMin;   // World-space AABB
    vec4 boundsMax;
    uvec4 info;       // y: flags (1 = GPU-drawn, 2 = never cull)
};

struct DrawCommand {  // VkDrawIndexedIndirectCommand
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct DrawGroup {
    uint base;   // First position of the group in the sorted order
    uint count;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer CullBuffer { CullData items[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer UintBuffer { uint values[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer CommandBuffer { DrawCommand commands[]; };
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer GroupBuffer { DrawGroup groups[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer PlaneBuffer { vec4 planes[6]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer StatsBuffer {
    uint visible;
    uint drawCommands;   // Commands with at least one instance
    uint triangles;
    uint reserved;
};

// One push-constant block for all three passes.
layout(push_constant) uniform CullPush {
    uvec2 cull;          // CullData per instance slot
    uvec2 order;         // Instance slot per position (group-sorted)
    uvec2 prefix;        // Per position: (exclusive prefix within its workgroup << 1) | visible
    uvec2 blockSums;     // Visible count per workgroup
    uvec2 blockOffsets;  // Exclusive prefix of blockSums; [blockCount] = total visible
    uvec2 groups;        // DrawGroup per command
    uvec2 commands;
    uvec2 visible;       // Output: instance slot per visible instance
    uvec2 stats;
    uvec2 planes;
    uint count;          // Positions (GPU-drawn instances)
    uint blockCount;
    uint groupCount;
    uint planeCount;
} pc;

#endif
