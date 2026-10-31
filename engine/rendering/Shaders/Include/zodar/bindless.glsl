// ZODAR engine shader interface: global bindless resources (descriptor set 0).
// Matches Engine/Renderer/BindlessRegistry.h.
#ifndef ZODAR_BINDLESS_GLSL
#define ZODAR_BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D zodarTextures[];
layout(set = 0, binding = 1) uniform sampler zodarSamplers[];
// Cube-map views live in the same texture table (binding 0); this alias reads them.
layout(set = 0, binding = 0) uniform textureCube zodarCubeTextures[];
// 2D array views (Renderer/TextureArray) live in the same table as well.
layout(set = 0, binding = 0) uniform texture2DArray zodarArrayTextures[];

vec4 zodarSample(uint textureIndex, uint samplerIndex, vec2 uv) {
    return texture(sampler2D(zodarTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]), uv);
}

vec4 zodarSampleLod(uint textureIndex, uint samplerIndex, vec2 uv, float lod) {
    return textureLod(sampler2D(zodarTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]),
                      uv, lod);
}

vec4 zodarSampleCubeLod(uint textureIndex, uint samplerIndex, vec3 direction, float lod) {
    return textureLod(samplerCube(zodarCubeTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]),
                      direction, lod);
}

// uvLayer.z is the array layer (not normalised).
vec4 zodarSampleArray(uint textureIndex, uint samplerIndex, vec3 uvLayer) {
    return texture(sampler2DArray(zodarArrayTextures[nonuniformEXT(textureIndex)], zodarSamplers[nonuniformEXT(samplerIndex)]),
                   uvLayer);
}

#endif
