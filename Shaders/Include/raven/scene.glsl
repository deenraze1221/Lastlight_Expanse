// RAVEN scene shader interface. Matches Engine/Renderer/SceneRenderer.h and Material.h.
// Every material shader (engine or game) uses this layout:
//   set 0: bindless textures/samplers + material table
//   push constants: addresses of frame constants and instance data, material index
#ifndef RAVEN_SCENE_GLSL
#define RAVEN_SCENE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#include "raven/bindless.glsl"

#define RAVEN_MATERIAL_ALPHA_MASK    1u
#define RAVEN_MATERIAL_ALPHA_BLEND   2u
#define RAVEN_MATERIAL_DOUBLE_SIDED  4u

struct RavenMaterial {
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

layout(set = 0, binding = 2, std430) readonly buffer RavenMaterials {
    RavenMaterial ravenMaterials[];
};

#define RAVEN_MAX_POINT_LIGHTS 64

struct RavenPointLight {
    vec4 positionRange;   // xyz: world position, w: range (0 = unbounded)
    vec4 colorIntensity;  // rgb: colour, a: intensity
};

// Per-frame constants, read through a buffer device address (frame scratch memory).
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer RavenFrameBuffer {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;   // xyz, w: time in seconds
    vec4 viewport;         // xy: size in pixels, zw: 1/size
    vec4 ambient;          // rgb * intensity
    vec4 sunDirection;     // xyz: direction the light travels, w: 1 if enabled
    vec4 sunColor;         // rgb * intensity
    uvec4 lightCounts;     // x: point lights
    RavenPointLight pointLights[RAVEN_MAX_POINT_LIGHTS];
};

struct RavenInstance {
    mat4 model;
    mat4 normalMatrix;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer RavenInstanceBuffer {
    RavenInstance instances[];
};

layout(push_constant) uniform RavenDraw {
    uvec2 frameAddress;     // RavenFrameBuffer
    uvec2 instanceAddress;  // RavenInstanceBuffer; indexed with gl_InstanceIndex
    uint materialIndex;
} ravenDraw;

#define ravenFrame RavenFrameBuffer(ravenDraw.frameAddress)
#define ravenInstances RavenInstanceBuffer(ravenDraw.instanceAddress).instances

RavenMaterial ravenMaterial() {
    return ravenMaterials[ravenDraw.materialIndex];
}

#endif
