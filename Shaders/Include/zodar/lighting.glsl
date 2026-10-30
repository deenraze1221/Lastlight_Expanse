// Physically based lighting (metallic-roughness): GGX distribution, height-correlated
// Smith visibility, Schlick Fresnel, Lambert diffuse. One shading model among others;
// materials may use their own lighting functions instead.
#ifndef ZODAR_LIGHTING_GLSL
#define ZODAR_LIGHTING_GLSL

#include "zodar/scene.glsl"

const float ZODAR_PI = 3.14159265359;

struct ZodarPbrSurface {
    vec3 position;
    vec3 normal;
    vec3 view;        // Unit vector from the surface to the camera.
    vec3 albedo;
    float metallic;
    float roughness;  // Perceptual roughness.
    float occlusion;
};

float zodarDistributionGgx(float nDotH, float alpha) {
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (ZODAR_PI * d * d);
}

float zodarVisibilitySmithGgx(float nDotV, float nDotL, float alpha) {
    float a2 = alpha * alpha;
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6);
}

vec3 zodarFresnelSchlick(float vDotH, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

// Radiance reflected toward the viewer for one light of incoming irradiance E along l.
vec3 zodarBrdf(ZodarPbrSurface s, vec3 l, vec3 irradiance) {
    vec3 h = normalize(l + s.view);
    float nDotL = max(dot(s.normal, l), 0.0);
    float nDotV = max(dot(s.normal, s.view), 1e-4);
    float nDotH = max(dot(s.normal, h), 0.0);
    float vDotH = max(dot(s.view, h), 0.0);
    float alpha = max(s.roughness * s.roughness, 0.002);

    vec3 f0 = mix(vec3(0.04), s.albedo, s.metallic);
    vec3 f = zodarFresnelSchlick(vDotH, f0);
    vec3 specular = f * zodarDistributionGgx(nDotH, alpha) * zodarVisibilitySmithGgx(nDotV, nDotL, alpha);
    vec3 diffuse = (1.0 - f) * (1.0 - s.metallic) * s.albedo / ZODAR_PI;
    return (diffuse + specular) * irradiance * nDotL;
}

// Inverse-square falloff with a smooth window reaching zero at `range`.
float zodarPointAttenuation(float distanceSq, float range) {
    float falloff = 1.0 / max(distanceSq, 1e-4);
    if (range <= 0.0) {
        return falloff;
    }
    float ratio = distanceSq / (range * range);
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    return falloff * window * window;
}

vec3 zodarShadePbr(ZodarPbrSurface s) {
    vec3 color = vec3(0.0);
    if (zodarFrame.sunDirection.w > 0.5) {
        color += zodarBrdf(s, -normalize(zodarFrame.sunDirection.xyz), zodarFrame.sunColor.rgb);
    }
    uint count = min(zodarFrame.lightCounts.x, uint(ZODAR_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < count; ++i) {
        ZodarPointLight light = zodarFrame.pointLights[i];
        vec3 toLight = light.positionRange.xyz - s.position;
        float distanceSq = dot(toLight, toLight);
        vec3 irradiance = light.colorIntensity.rgb * light.colorIntensity.a *
                          zodarPointAttenuation(distanceSq, light.positionRange.w);
        color += zodarBrdf(s, toLight * inversesqrt(max(distanceSq, 1e-8)), irradiance);
    }
    // Constant ambient term until image-based lighting exists.
    vec3 diffuseColor = s.albedo * (1.0 - s.metallic);
    vec3 f0 = mix(vec3(0.04), s.albedo, s.metallic);
    color += (diffuseColor + f0 * 0.5) * zodarFrame.ambient.rgb * s.occlusion;
    return color;
}

#endif
