#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven/bindless.glsl"

// Copies the HDR scene colour to the output. No tone mapping yet: values are clamped
// to [0,1]. The post-processing stack (Milestone 3) replaces this pass.
// Display outputs must be sRGB-encoded: _SRGB formats do it in hardware; for UNORM
// outputs (some surfaces offer no sRGB format) the shader encodes.
layout(push_constant) uniform Push {
    uint sourceTexture;
    uint sourceSampler;
    uint encodeSrgb;
} pc;

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = ravenSampleLod(pc.sourceTexture, pc.sourceSampler, inUv, 0.0).rgb;
    color = clamp(color, 0.0, 1.0);
    outColor = vec4(pc.encodeSrgb != 0u ? linearToSrgb(color) : color, 1.0);
}
