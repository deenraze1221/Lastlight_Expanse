// Clustered Forward+ data shared by the cull compute shader and material shaders.
// Mirrors Engine/Renderer/Lighting/ClusterMath.h and ClusteredLighting.h.
#ifndef ZODAR_CLUSTERS_GLSL
#define ZODAR_CLUSTERS_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#define ZODAR_LIGHT_POINT 0u
#define ZODAR_LIGHT_SPOT  1u

// 96 bytes. World-space data for shading, view-space data for culling.
struct ZodarLight {
    vec4 positionRange;     // xyz: world position, w: range (> 0; smooth window reaches 0 there)
    vec4 colorType;         // rgb: colour * intensity, a: type (uintBitsToFloat)
    vec4 directionSpot;     // xyz: world direction the spot light points to, w: spot scale
    vec4 spotOffsetCone;    // x: spot offset, y: cos(outer), z: sin(outer), w: unused
    vec4 viewPositionRange; // view-space position, range
    vec4 viewDirection;     // view-space spot direction
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ZodarLightBuffer {
    ZodarLight lights[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) buffer ZodarClusterCounts {
    uint counts[];
};
// Two 16-bit light indices per uint; cluster c owns [c * maxPerCluster / 2, ...).
layout(buffer_reference, std430, buffer_reference_align = 4) buffer ZodarClusterIndices {
    uint pairs[];
};

// Slice of a fragment at positive view depth (exponential slicing).
uint zodarClusterSlice(float depth, vec4 clusterDepth, uint slices) {
    float s = floor(log(max(depth, 1e-6)) * clusterDepth.x + clusterDepth.y);
    return uint(clamp(s, 0.0, float(slices - 1u)));
}

uint zodarClusterIndex(uvec3 cluster, uvec4 grid) {
    return (cluster.z * grid.y + cluster.y) * grid.x + cluster.x;
}

uint zodarClusterLight(ZodarClusterIndices indices, uint cluster, uint maxPerCluster, uint i) {
    uint pair = indices.pairs[(cluster * maxPerCluster + i) >> 1];
    return (i & 1u) == 0u ? (pair & 0xFFFFu) : (pair >> 16);
}

#endif
