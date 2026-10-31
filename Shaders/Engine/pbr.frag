#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/surface.glsl"
#include "zodar/lighting.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    ZodarMaterial m = zodarMaterial();
    vec4 base = zodarBaseColor(m);
    vec4 mr = zodarSample(m.metallicRoughnessTexture, m.metallicRoughnessSampler, vUv0); // glTF: G=roughness, B=metallic
    float ao = zodarSample(m.occlusionTexture, m.occlusionSampler, vUv0).r;

    ZodarPbrSurface s;
    s.position = vWorldPosition;
    s.normal = zodarShadingNormal(m);
    s.view = normalize(zodarFrame.cameraPosition.xyz - vWorldPosition);
    s.albedo = base.rgb;
    s.metallic = clamp(m.metallic * mr.b, 0.0, 1.0);
    s.roughness = clamp(m.roughness * mr.g, 0.03, 1.0);
    s.occlusion = mix(1.0, ao, m.occlusionStrength);

    ZodarLightingResult r = zodarShadePbrDetailed(s);
    vec3 color = r.direct + r.diffuseIndirect + r.specularIndirect + zodarEmissive(m);
    switch (zodarFrame.lightCounts.y) {
    case ZODAR_DEBUG_LIGHT_COUNT:
        // Magenta: the cluster's list is full (lights beyond the capacity are missing).
        color = r.clusterLights >= zodarFrame.lightCounts.w ? vec3(1.0, 0.0, 1.0) : zodarHeatMap(float(r.clusterLights) / 32.0);
        break;
    case ZODAR_DEBUG_CLUSTER_SLICE: color = zodarHeatMap(float(r.clusterSlice) / float(zodarFrame.clusterGrid.z - 1u)); break;
    case ZODAR_DEBUG_NORMALS: color = s.normal * 0.5 + 0.5; break;
    case ZODAR_DEBUG_DIRECT: color = r.direct; break;
    case ZODAR_DEBUG_INDIRECT: color = r.diffuseIndirect + r.specularIndirect; break;
    case ZODAR_DEBUG_SPECULAR_IBL: color = r.specularIndirect; break;
    default: break;
    }
    float alpha = (m.flags & ZODAR_MATERIAL_ALPHA_BLEND) != 0u ? base.a : 1.0;
    outColor = vec4(color, alpha);
}
