// ZODAR engine shader library: analytic distance + exponential height fog (style-neutral).
// Integrates density(h) = density * exp(-heightFalloff * (h - baseHeight)) along the view ray.
#ifndef ZODAR_FOG_GLSL
#define ZODAR_FOG_GLSL

struct ZodarFog {
    vec3 color;          // linear in-scattered colour
    float density;       // extinction per world unit at baseHeight
    float heightFalloff; // 0 = uniform fog; larger = fog hugs the ground
    float baseHeight;
    float startDistance; // no fog closer than this
    float maxOpacity;    // clamp so far geometry/sky stays faintly visible
};

// Fraction of the surface colour replaced by fog (0..maxOpacity).
float zodarFogAmount(ZodarFog fog, vec3 cameraPosition, vec3 worldPosition) {
    vec3 ray = worldPosition - cameraPosition;
    float dist = length(ray);
    float travelled = max(dist - fog.startDistance, 0.0);
    if (travelled <= 0.0 || fog.density <= 0.0) {
        return 0.0;
    }
    float optical;
    float dy = ray.y * (travelled / max(dist, 1e-4));
    float h0 = cameraPosition.y + ray.y * (1.0 - travelled / max(dist, 1e-4)) - fog.baseHeight;
    if (fog.heightFalloff > 0.0 && abs(dy) > 1e-3) {
        float k = fog.heightFalloff;
        optical = fog.density * exp(-k * h0) * (1.0 - exp(-k * dy)) / (k * dy) * travelled;
    } else {
        optical = fog.density * exp(-fog.heightFalloff * h0) * travelled;
    }
    return min(1.0 - exp(-optical), fog.maxOpacity);
}

vec3 zodarApplyFog(ZodarFog fog, vec3 color, vec3 cameraPosition, vec3 worldPosition) {
    return mix(color, fog.color, zodarFogAmount(fog, cameraPosition, worldPosition));
}

#ifdef ZODAR_SCENE_GLSL
// The frame's fog (SceneLighting::fog); returns `color` unchanged when fog is off.
vec3 zodarApplyFrameFog(vec3 color, vec3 worldPosition) {
    if (zodarFrame.fogColor.w <= 0.0) {
        return color;
    }
    ZodarFog fog;
    fog.color = zodarFrame.fogColor.rgb;
    fog.density = zodarFrame.fogColor.w;
    fog.heightFalloff = zodarFrame.fogParams.x;
    fog.baseHeight = zodarFrame.fogParams.y;
    fog.startDistance = zodarFrame.fogParams.z;
    fog.maxOpacity = zodarFrame.fogParams.w;
    return zodarApplyFog(fog, color, zodarFrame.cameraPosition.xyz, worldPosition);
}
#endif

#endif
