// Shared helpers for the image-based-lighting bake (compute). Cube faces follow the Vulkan
// order +X, -X, +Y, -Y, +Z, -Z; the equirectangular map has +Y at the top row and
// u = atan(z, x) / 2pi + 0.5.
#ifndef ZODAR_IBL_COMMON_GLSL
#define ZODAR_IBL_COMMON_GLSL

const float IBL_PI = 3.14159265359;

vec3 cubeDirection(uint face, vec2 uv) { // uv in [-1, 1], v pointing down the face
    switch (face) {
    case 0u: return normalize(vec3(1.0, -uv.y, -uv.x));
    case 1u: return normalize(vec3(-1.0, -uv.y, uv.x));
    case 2u: return normalize(vec3(uv.x, 1.0, uv.y));
    case 3u: return normalize(vec3(uv.x, -1.0, -uv.y));
    case 4u: return normalize(vec3(uv.x, -uv.y, 1.0));
    default: return normalize(vec3(-uv.x, -uv.y, -1.0));
    }
}

vec3 texelDirection(uvec3 id, uint size) {
    vec2 uv = (vec2(id.xy) + 0.5) / float(size) * 2.0 - 1.0;
    return cubeDirection(id.z, uv);
}

vec2 equirectUv(vec3 d) {
    return vec2(atan(d.z, d.x) / (2.0 * IBL_PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / IBL_PI);
}

vec2 hammersley(uint i, uint n) {
    uint bits = bitfieldReverse(i);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

// Tangent frame around n.
mat3 tangentFrame(vec3 n) {
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    return mat3(t, cross(n, t), n);
}

// GGX half vector (tangent space, z = normal) for alpha = roughness^2.
vec3 importanceSampleGgx(vec2 xi, float alpha) {
    float phi = 2.0 * IBL_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float ggxD(float nDotH, float alpha) {
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (IBL_PI * d * d);
}

// Same as zodarVisibilitySmithGgx (height-correlated Smith / (4 NoL NoV)).
float smithVisibility(float nDotV, float nDotL, float alpha) {
    float a2 = alpha * alpha;
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6);
}

#endif
