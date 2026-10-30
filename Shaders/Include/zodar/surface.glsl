// Fragment-side varyings and material surface helpers shared by engine fragment shaders.
#ifndef ZODAR_SURFACE_GLSL
#define ZODAR_SURFACE_GLSL

#include "zodar/scene.glsl"

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUv0;
layout(location = 4) in vec4 vColor;
layout(location = 5) flat in uint vMaterialIndex;

// Material of the instance being shaded (index written per instance, see vertex.glsl).
ZodarMaterial zodarMaterial() {
    return zodarMaterials[vMaterialIndex];
}

// Base colour (factor * texture * vertex colour), with alpha-mask discard applied.
vec4 zodarBaseColor(ZodarMaterial m) {
    vec4 color = m.baseColorFactor * zodarSample(m.baseColorTexture, m.baseColorSampler, vUv0) * vColor;
    if ((m.flags & ZODAR_MATERIAL_ALPHA_MASK) != 0u && color.a < m.emissiveAlphaCutoff.a) {
        discard;
    }
    return color;
}

// Shading normal from the interpolated TBN and the material normal map.
vec3 zodarShadingNormal(ZodarMaterial m) {
    vec3 n = normalize(vNormal);
    if ((m.flags & ZODAR_MATERIAL_DOUBLE_SIDED) != 0u && !gl_FrontFacing) {
        n = -n;
    }
    vec3 t = vTangent.xyz - n * dot(n, vTangent.xyz);
    float tLen = length(t);
    if (tLen < 1e-6) {
        return n;
    }
    t /= tLen;
    vec3 b = cross(n, t) * vTangent.w;
    vec3 tn = zodarSample(m.normalTexture, m.normalSampler, vUv0).xyz * 2.0 - 1.0;
    tn.xy *= m.normalScale;
    return normalize(mat3(t, b, n) * tn);
}

vec3 zodarEmissive(ZodarMaterial m) {
    return m.emissiveAlphaCutoff.rgb * zodarSample(m.emissiveTexture, m.emissiveSampler, vUv0).rgb;
}

#endif
