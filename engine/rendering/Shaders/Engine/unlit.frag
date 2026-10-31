#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/surface.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    ZodarMaterial m = zodarMaterial();
    vec4 base = zodarBaseColor(m);
    float alpha = (m.flags & ZODAR_MATERIAL_ALPHA_BLEND) != 0u ? base.a : 1.0;
    outColor = vec4(base.rgb + zodarEmissive(m), alpha);
}
