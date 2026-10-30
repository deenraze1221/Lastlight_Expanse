#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven/surface.glsl"
#include "raven/lighting.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    RavenMaterial m = ravenMaterial();
    vec4 base = ravenBaseColor(m);
    vec4 mr = ravenSample(m.metallicRoughnessTexture, m.metallicRoughnessSampler, vUv0); // glTF: G=roughness, B=metallic
    float ao = ravenSample(m.occlusionTexture, m.occlusionSampler, vUv0).r;

    RavenPbrSurface s;
    s.position = vWorldPosition;
    s.normal = ravenShadingNormal(m);
    s.view = normalize(ravenFrame.cameraPosition.xyz - vWorldPosition);
    s.albedo = base.rgb;
    s.metallic = clamp(m.metallic * mr.b, 0.0, 1.0);
    s.roughness = clamp(m.roughness * mr.g, 0.03, 1.0);
    s.occlusion = mix(1.0, ao, m.occlusionStrength);

    vec3 color = ravenShadePbr(s) + ravenEmissive(m);
    float alpha = (m.flags & RAVEN_MATERIAL_ALPHA_BLEND) != 0u ? base.a : 1.0;
    outColor = vec4(color, alpha);
}
