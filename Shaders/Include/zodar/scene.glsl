// ZODAR scene shader interface. Matches Engine/Renderer/SceneRenderer.h and Material.h.
// Every material shader (engine or game) uses this layout:
//   set 0: bindless textures/samplers + material table
//   push constants: addresses of frame constants, instance data and the visible-instance list
#ifndef ZODAR_SCENE_GLSL
#define ZODAR_SCENE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#include "zodar/bindless.glsl"

#define ZODAR_MATERIAL_ALPHA_MASK    1u
#define ZODAR_MATERIAL_ALPHA_BLEND   2u
#define ZODAR_MATERIAL_DOUBLE_SIDED  4u

struct ZodarMaterial {
    vec4 baseColorFactor;
    vec4 emissiveAlphaCutoff;   // rgb: emissive radiance, a: alpha cutoff
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    uint baseColorTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
    uint baseColorSampler;
    uint metallicRoughnessSampler;
    uint normalSampler;
    uint occlusionSampler;
    uint emissiveSampler;
    uint flags;
    uint reserved;
    vec4 custom0;               // Free parameters for game/custom shaders.
    vec4 custom1;
};

layout(set = 0, binding = 2, std430) readonly buffer ZodarMaterials {
    ZodarMaterial zodarMaterials[];
};

// Lighting flags (ZodarFrameBuffer.lightCounts.z).
#define ZODAR_LIGHTING_IBL          1u   // Image-based lighting from the frame's environment
#define ZODAR_LIGHTING_MULTISCATTER 2u   // GGX multiple-scattering energy compensation
#define ZODAR_LIGHTING_BRUTE_FORCE  4u   // Debug/reference: every light for every fragment

// Debug views (ZodarFrameBuffer.lightCounts.y).
#define ZODAR_DEBUG_NONE          0u
#define ZODAR_DEBUG_LIGHT_COUNT   1u  // Lights in the fragment's cluster (heat map)
#define ZODAR_DEBUG_CLUSTER_SLICE 2u  // Depth slice index
#define ZODAR_DEBUG_NORMALS       3u  // World-space shading normal
#define ZODAR_DEBUG_DIRECT        4u  // Direct lighting only
#define ZODAR_DEBUG_INDIRECT      5u  // Image-based (or ambient) lighting only
#define ZODAR_DEBUG_SPECULAR_IBL  6u  // Specular IBL only

// Per-frame constants, read through a buffer device address (frame scratch memory).
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ZodarFrameBuffer {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;   // xyz, w: time in seconds
    vec4 viewport;         // xy: size in pixels, zw: 1/size
    vec4 ambient;          // rgb * intensity (used when no environment is set)
    vec4 sunDirection;     // xyz: direction the light travels, w: 1 if enabled
    vec4 sunColor;         // rgb * intensity
    uvec4 lightCounts;     // x: lights, y: debug view, z: lighting flags, w: max lights per cluster
    uvec4 clusterGrid;     // xyz: cluster counts, w: tile size in pixels
    vec4 clusterDepth;     // x: slice scale, y: slice bias, z: near, w: far
    uvec2 lightsAddress;   // ZodarLightBuffer
    uvec2 clusterCountsAddress;
    uvec2 clusterIndicesAddress;
    uvec2 reserved0;
    uvec4 ibl;             // x: irradiance cube, y: prefiltered cube, z: DFG LUT (bindless slots), w: sampler
    vec4 iblParams;        // x: intensity, y: prefiltered mip count, z: skybox mip
};

struct ZodarInstance {
    mat4 model;
    mat4 normalMatrix;
    uvec4 data;            // x: material index
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ZodarInstanceBuffer {
    ZodarInstance instances[];
};

// Instance indices of the current draw: instance = list[gl_InstanceIndex]. The CPU path
// writes 0..n-1; the GPU-driven path writes the instances that survived GPU culling.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ZodarIndexBuffer {
    uint indices[];
};

layout(push_constant) uniform ZodarDraw {
    uvec2 frameAddress;     // ZodarFrameBuffer
    uvec2 instanceAddress;  // ZodarInstanceBuffer
    uvec2 visibleAddress;   // ZodarIndexBuffer
} zodarDraw;

#define zodarFrame ZodarFrameBuffer(zodarDraw.frameAddress)
#define zodarInstances ZodarInstanceBuffer(zodarDraw.instanceAddress).instances



#endif
