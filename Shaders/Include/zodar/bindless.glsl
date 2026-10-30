// ZODAR engine shader interface: global bindless resources (descriptor set 0).
// Matches Engine/Renderer/BindlessRegistry.h.
#ifndef ZODAR_BINDLESS_GLSL
#define ZODAR_BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D zodarTextures[];
layout(set = 0, binding = 1) uniform sampler zodarSamplers[];

vec4 zodarSample(uint textureIndex, uint samplerIndex, vec2 uv) {
    return texture(sampler2D(zodarTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]), uv);
}

vec4 zodarSampleLod(uint textureIndex, uint samplerIndex, vec2 uv, float lod) {
    return textureLod(sampler2D(zodarTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]),
                      uv, lod);
}

#endif
