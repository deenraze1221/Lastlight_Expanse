#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/bindless.glsl"

// Environment background: radiance cube along the view ray.
layout(push_constant) uniform Push {
    mat4 inverseViewProjection; // Of the camera rotation only (no translation)
    uint cube;
    uint sampler_;
    float intensity;
    float lod;
} pc;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 p = pc.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 direction = normalize(p.xyz / p.w);
    outColor = vec4(zodarSampleCubeLod(pc.cube, pc.sampler_, direction, pc.lod).rgb * pc.intensity, 1.0);
}
