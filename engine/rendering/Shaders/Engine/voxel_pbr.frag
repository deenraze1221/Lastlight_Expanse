#version 450
#extension GL_GOOGLE_include_directive : require
// ZODAR voxel block surface ("zodar/voxel", Phase V2; docs/guide/VOXELS.md §9): texture-array
// blocks with per-layer PBR parameters, the same lighting and fog as zodar/pbr.
//
// Vertex colour (ChunkMesher with VoxelPalette::faceLayers): rgb = material tint * baked AO,
// a = texture-array layer. UVs: mesher UVs (vUv0, MeshOptions::uvPerMetre), so shapes and
// slopes are textured like cube faces.
// Material: custom0.x texture array (uint bits), custom0.y sampler (uint bits),
//           custom0.z layer parameter table (Renderer/Vec4Table, uint bits),
//           custom0.w table size (uint bits), custom1.x nearest sampler for the table
//           (uint bits); baseColorFactor multiplies the albedo.
// Layer parameters (float32, exact): r = roughness, g = metallic, b = emissive strength.
// Layers outside the table (a mesh from another palette, a non-voxel mesh) never read past
// it: they use the fallback parameters (roughness 1, metallic 0, emissive 0); the texture
// sample clamps its layer as Vulkan defines for array layers.
// Emissive: texels with alpha < 1 emit texel.rgb * (1 - alpha) * strength (not tinted).
#include "zodar/surface.glsl"
#include "zodar/lighting.glsl"
#include "zodar/fog.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    ZodarMaterial m = zodarMaterial();
    const uint textureArray = floatBitsToUint(m.custom0.x);
    const uint samplerIndex = floatBitsToUint(m.custom0.y);
    const uint table = floatBitsToUint(m.custom0.z);
    const uint tableSize = floatBitsToUint(m.custom0.w);
    const uint tableSampler = floatBitsToUint(m.custom1.x);
    const float layerValue = floor(vColor.a + 0.5);
    vec4 texel = zodarSampleArray(textureArray, samplerIndex, vec3(vUv0, layerValue));
    // Bounds check in float before converting: NaN, negative or too-large values fail it.
    vec4 params = vec4(1.0, 0.0, 0.0, 0.0);
    if (layerValue >= 0.0 && layerValue < float(tableSize)) {
        params = zodarFetch(table, tableSampler, ivec2(int(layerValue), 0));
    }

    ZodarPbrSurface s;
    s.position = vWorldPosition;
    s.normal = normalize(vNormal);
    s.view = normalize(zodarFrame.cameraPosition.xyz - vWorldPosition);
    s.albedo = texel.rgb * vColor.rgb * m.baseColorFactor.rgb;
    s.metallic = clamp(params.g, 0.0, 1.0);
    s.roughness = clamp(params.r, 0.03, 1.0);
    s.occlusion = 1.0;

    ZodarLightingResult r = zodarShadePbrDetailed(s);
    vec3 color = r.direct + r.diffuseIndirect + r.specularIndirect + texel.rgb * (1.0 - texel.a) * params.b;
    switch (zodarFrame.lightCounts.y) {
    case ZODAR_DEBUG_LIGHT_COUNT:
        color = r.clusterLights >= zodarFrame.lightCounts.w ? vec3(1.0, 0.0, 1.0) : zodarHeatMap(float(r.clusterLights) / 32.0);
        break;
    case ZODAR_DEBUG_CLUSTER_SLICE: color = zodarHeatMap(float(r.clusterSlice) / float(zodarFrame.clusterGrid.z - 1u)); break;
    case ZODAR_DEBUG_NORMALS: color = s.normal * 0.5 + 0.5; break;
    case ZODAR_DEBUG_DIRECT: color = r.direct; break;
    case ZODAR_DEBUG_INDIRECT: color = r.diffuseIndirect + r.specularIndirect; break;
    case ZODAR_DEBUG_SPECULAR_IBL: color = r.specularIndirect; break;
    default: color = zodarApplyFrameFog(color, vWorldPosition); break;
    }
    outColor = vec4(color, 1.0);
}
