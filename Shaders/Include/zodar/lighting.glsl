// Physically based lighting (metallic-roughness), one shading model among others; materials
// may use their own lighting functions instead.
//   Direct:   Cook-Torrance (GGX distribution, height-correlated Smith visibility,
//             Fresnel-Schlick) + Lambert diffuse, multiple-scattering energy compensation
//             (Kulla-Conty / Filament), inverse-square attenuation with a smooth range window.
//   Lights:   sun + clustered point/spot lights (Clustered Forward+).
//   Indirect: image-based lighting (irradiance cube, GGX-prefiltered cube, DFG LUT; split sum)
//             or a constant ambient term when no environment is set.
#ifndef ZODAR_LIGHTING_GLSL
#define ZODAR_LIGHTING_GLSL

#include "zodar/scene.glsl"
#include "zodar/clusters.glsl"

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

struct ZodarLightingResult {
    vec3 direct;
    vec3 diffuseIndirect;
    vec3 specularIndirect;
    uint clusterLights;   // Lights evaluated from the fragment's cluster
    uint clusterSlice;
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

// Precomputed shading terms shared by all lights of one fragment.
struct ZodarShadingContext {
    vec3 f0;
    vec3 diffuseColor;
    float alpha;
    float nDotV;
    vec2 dfg;                // x: sum(Gv * Fc), y: sum(Gv) (Filament multiscatter DFG)
    vec3 energyCompensation; // 1 + f0 * (1 / dfg.y - 1)
};

// Analytic DFG fit (Karis, mobile) when no LUT is bound; returns the same (x, y) convention.
vec2 zodarDfgApprox(float nDotV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw; // f0 * ab.x + ab.y
    return vec2(ab.y, ab.x + ab.y);
}

ZodarShadingContext zodarShadingContext(ZodarPbrSurface s) {
    ZodarShadingContext c;
    c.f0 = mix(vec3(0.04), s.albedo, s.metallic);
    c.diffuseColor = s.albedo * (1.0 - s.metallic);
    c.alpha = max(s.roughness * s.roughness, 0.002);
    c.nDotV = max(dot(s.normal, s.view), 1e-4);
    if (zodarFrame.ibl.z != 0xFFFFFFFFu) {
        c.dfg = zodarSampleLod(zodarFrame.ibl.z, zodarFrame.ibl.w, vec2(c.nDotV, s.roughness), 0.0).xy;
    } else {
        c.dfg = zodarDfgApprox(c.nDotV, s.roughness);
    }
    bool multiscatter = (zodarFrame.lightCounts.z & ZODAR_LIGHTING_MULTISCATTER) != 0u;
    c.energyCompensation = multiscatter ? 1.0 + c.f0 * (1.0 / max(c.dfg.y, 1e-4) - 1.0) : vec3(1.0);
    return c;
}

// Radiance reflected toward the viewer for incoming irradiance E (at normal incidence) along l.
vec3 zodarBrdf(ZodarPbrSurface s, ZodarShadingContext c, vec3 l, vec3 irradiance) {
    float nDotL = max(dot(s.normal, l), 0.0);
    vec3 h = normalize(l + s.view);
    float nDotH = max(dot(s.normal, h), 0.0);
    float vDotH = max(dot(s.view, h), 0.0);
    vec3 f = zodarFresnelSchlick(vDotH, c.f0);
    vec3 specular = f * zodarDistributionGgx(nDotH, c.alpha) * zodarVisibilitySmithGgx(c.nDotV, nDotL, c.alpha);
    vec3 diffuse = (1.0 - f) * c.diffuseColor / ZODAR_PI;
    return (diffuse + specular * c.energyCompensation) * irradiance * nDotL;
}

// Inverse-square falloff with a smooth window reaching zero at `range` (Karis 2013).
float zodarDistanceAttenuation(float distanceSq, float range) {
    float falloff = 1.0 / max(distanceSq, 1e-4);
    float ratio = distanceSq / (range * range);
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    return falloff * window * window;
}

vec3 zodarLightContribution(ZodarPbrSurface s, ZodarShadingContext c, ZodarLight light) {
    vec3 toLight = light.positionRange.xyz - s.position;
    float distanceSq = dot(toLight, toLight);
    vec3 l = toLight * inversesqrt(max(distanceSq, 1e-8));
    float attenuation = zodarDistanceAttenuation(distanceSq, light.positionRange.w);
    if (floatBitsToUint(light.colorType.a) == ZODAR_LIGHT_SPOT) {
        float cd = dot(-l, light.directionSpot.xyz);
        float a = clamp(cd * light.directionSpot.w + light.spotOffsetCone.x, 0.0, 1.0);
        attenuation *= a * a;
    }
    return zodarBrdf(s, c, l, light.colorType.rgb * attenuation);
}

// Specular occlusion from ambient occlusion (Lagarde & de Rousiers 2014).
float zodarSpecularOcclusion(float nDotV, float ao, float alpha) {
    return clamp(pow(nDotV + ao, exp2(-16.0 * alpha - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

ZodarLightingResult zodarShadePbrDetailed(ZodarPbrSurface s) {
    ZodarLightingResult r;
    r.direct = vec3(0.0);
    r.clusterLights = 0u;
    ZodarShadingContext c = zodarShadingContext(s);
    if (zodarFrame.sunDirection.w > 0.5) {
        r.direct += zodarBrdf(s, c, -normalize(zodarFrame.sunDirection.xyz), zodarFrame.sunColor.rgb);
    }

    // Clustered point and spot lights.
    float depth = -dot(transpose(zodarFrame.view)[2], vec4(s.position, 1.0));
    uvec4 grid = zodarFrame.clusterGrid;
    r.clusterSlice = zodarClusterSlice(depth, zodarFrame.clusterDepth, grid.z);
    uint lightCount = zodarFrame.lightCounts.x;
    ZodarLightBuffer lights = ZodarLightBuffer(zodarFrame.lightsAddress);
    if (lightCount > 0u) {
        if ((zodarFrame.lightCounts.z & ZODAR_LIGHTING_BRUTE_FORCE) != 0u) {
            for (uint i = 0u; i < lightCount; ++i) {
                r.direct += zodarLightContribution(s, c, lights.lights[i]);
            }
            r.clusterLights = lightCount;
        } else {
            uvec2 tile = min(uvec2(gl_FragCoord.xy) / grid.w, grid.xy - 1u);
            uint cluster = zodarClusterIndex(uvec3(tile, r.clusterSlice), grid);
            uint count = ZodarClusterCounts(zodarFrame.clusterCountsAddress).counts[cluster];
            ZodarClusterIndices indices = ZodarClusterIndices(zodarFrame.clusterIndicesAddress);
            uint maxPerCluster = zodarFrame.lightCounts.w;
            for (uint i = 0u; i < count; ++i) {
                r.direct += zodarLightContribution(s, c, lights.lights[zodarClusterLight(indices, cluster, maxPerCluster, i)]);
            }
            r.clusterLights = count;
        }
    }

    // Indirect lighting.
    if ((zodarFrame.lightCounts.z & ZODAR_LIGHTING_IBL) != 0u) {
        float intensity = zodarFrame.iblParams.x;
        vec3 irradiance = zodarSampleCubeLod(zodarFrame.ibl.x, zodarFrame.ibl.w, s.normal, 0.0).rgb;
        vec3 reflected = reflect(-s.view, s.normal);
        float lod = s.roughness * (zodarFrame.iblParams.y - 1.0);
        vec3 prefiltered = zodarSampleCubeLod(zodarFrame.ibl.y, zodarFrame.ibl.w, reflected, lod).rgb;
        vec3 specularAlbedo = mix(c.dfg.xxx, c.dfg.yyy, c.f0); // Single-scattering E(f0)
        // Energy conservation: diffuse only receives what single scattering did not reflect.
        r.diffuseIndirect = irradiance * c.diffuseColor * (1.0 - specularAlbedo) * s.occlusion * intensity;
        r.specularIndirect = prefiltered * specularAlbedo * c.energyCompensation *
                             zodarSpecularOcclusion(c.nDotV, s.occlusion, c.alpha) * intensity;
    } else {
        r.diffuseIndirect = c.diffuseColor * zodarFrame.ambient.rgb * s.occlusion;
        r.specularIndirect = c.f0 * 0.5 * zodarFrame.ambient.rgb * s.occlusion;
    }
    return r;
}

vec3 zodarShadePbr(ZodarPbrSurface s) {
    ZodarLightingResult r = zodarShadePbrDetailed(s);
    return r.direct + r.diffuseIndirect + r.specularIndirect;
}

// Heat map for light counts (0 = dark blue ... >= max = red).
vec3 zodarHeatMap(float t) {
    t = clamp(t, 0.0, 1.0);
    return clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0);
}

#endif
