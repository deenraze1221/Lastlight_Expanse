#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/bindless.glsl"

layout(push_constant) uniform Push {
    uint textureIndex;
    uint samplerIndex;
    float lod;
} pc;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = zodarSampleLod(pc.textureIndex, pc.samplerIndex, inUv, pc.lod);
}
