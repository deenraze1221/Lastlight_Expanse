#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/bindless.glsl"

// Same push layout as test_sample.frag; `layer` replaces the LOD.
layout(push_constant) uniform Push {
    uint textureIndex;
    uint samplerIndex;
    float layer;
} pc;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = zodarSampleArray(pc.textureIndex, pc.samplerIndex, vec3(inUv, pc.layer));
}
