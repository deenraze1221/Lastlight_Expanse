// Fragment-side varyings and material surface helpers shared by engine fragment shaders.
#ifndef RAVEN_SURFACE_GLSL
#define RAVEN_SURFACE_GLSL

#include "raven/scene.glsl"

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUv0;
layout(location = 4) in vec4 vColor;

// Base colour (factor * texture * vertex colour), with alpha-mask discard applied.
vec4 ravenBaseColor(RavenMaterial m) {
    vec4 color = m.baseColorFactor * ravenSample(m.baseColorTexture, m.baseColorSampler, vUv0) * vColor;
    if ((m.flags & RAVEN_MATERIAL_ALPHA_MASK) != 0u && color.a < m.emissiveAlphaCutoff.a) {
        discard;
    }
    return color;
}

// Shading normal from the interpolated TBN and the material normal map.
vec3 ravenShadingNormal(RavenMaterial m) {
    vec3 n = normalize(vNormal);
    if ((m.flags & RAVEN_MATERIAL_DOUBLE_SIDED) != 0u && !gl_FrontFacing) {
        n = -n;
    }
    vec3 t = vTangent.xyz - n * dot(n, vTangent.xyz);
    float tLen = length(t);
    if (tLen < 1e-6) {
        return n;
    }
    t /= tLen;
    vec3 b = cross(n, t) * vTangent.w;
    vec3 tn = ravenSample(m.normalTexture, m.normalSampler, vUv0).xyz * 2.0 - 1.0;
    tn.xy *= m.normalScale;
    return normalize(mat3(t, b, n) * tn);
}

vec3 ravenEmissive(RavenMaterial m) {
    return m.emissiveAlphaCutoff.rgb * ravenSample(m.emissiveTexture, m.emissiveSampler, vUv0).rgb;
}

#endif
