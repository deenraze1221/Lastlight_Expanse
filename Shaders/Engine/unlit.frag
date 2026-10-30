#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven/surface.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    RavenMaterial m = ravenMaterial();
    vec4 base = ravenBaseColor(m);
    float alpha = (m.flags & RAVEN_MATERIAL_ALPHA_BLEND) != 0u ? base.a : 1.0;
    outColor = vec4(base.rgb + ravenEmissive(m), alpha);
}
