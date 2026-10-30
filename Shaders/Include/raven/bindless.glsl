// RAVEN engine shader interface: global bindless resources (descriptor set 0).
// Matches Engine/Renderer/BindlessRegistry.h.
#ifndef RAVEN_BINDLESS_GLSL
#define RAVEN_BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D ravenTextures[];
layout(set = 0, binding = 1) uniform sampler ravenSamplers[];

vec4 ravenSample(uint textureIndex, uint samplerIndex, vec2 uv) {
    return texture(sampler2D(ravenTextures[nonuniformEXT(textureIndex)], ravenSamplers[nonuniformEXT(samplerIndex)]), uv);
}

vec4 ravenSampleLod(uint textureIndex, uint samplerIndex, vec2 uv, float lod) {
    return textureLod(sampler2D(ravenTextures[nonuniformEXT(textureIndex)], ravenSamplers[nonuniformEXT(samplerIndex)]),
                      uv, lod);
}

#endif
