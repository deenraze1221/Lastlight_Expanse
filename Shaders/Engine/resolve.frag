#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven/bindless.glsl"

// Copies the HDR scene colour to the output. No tone mapping yet: values are clamped
// to [0,1]. The post-processing stack (Milestone 3) replaces this pass.
layout(push_constant) uniform Push {
    uint sourceTexture;
    uint sourceSampler;
} pc;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = ravenSampleLod(pc.sourceTexture, pc.sourceSampler, inUv, 0.0).rgb;
    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
