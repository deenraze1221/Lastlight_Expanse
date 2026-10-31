#version 450
#extension GL_GOOGLE_include_directive : require
// Lastlight Expanse voxel surface: crisp pixel-art texture arrays, engine PBR lighting
// (sun, clustered lights, IBL) and the frame fog.
//
// Vertex colour: rgb = palette tint * baked ambient occlusion, a = texture layer
// (>= 1000: hero array, layer - 1000). Texel alpha < 1 marks emissive pixels.
// Material: custom0.x standard array (uint bits), custom0.y sampler (uint bits),
//           custom0.z hero array (uint bits); custom1.x emissive strength, custom1.y voxels per unit.
#include "zodar/surface.glsl"
#include "zodar/lighting.glsl"
#include "zodar/fog.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    ZodarMaterial m = zodarMaterial();
    vec3 n = normalize(vNormal);
    vec3 p = vWorldPosition * m.custom1.y;
    // World-aligned UVs: texture "up" is +Y on side faces, so faces need no per-vertex UVs.
    vec2 uv;
    if (abs(n.y) > 0.5) {
        uv = vec2(p.x, p.z);
    } else if (abs(n.x) > 0.5) {
        uv = vec2(n.x > 0.0 ? -p.z : p.z, -p.y);
    } else {
        uv = vec2(n.z > 0.0 ? p.x : -p.x, -p.y);
    }
    float layer = floor(vColor.a + 0.5);
    uint array = floatBitsToUint(m.custom0.x);
    if (layer >= 1000.0) {
        array = floatBitsToUint(m.custom0.z);
        layer -= 1000.0;
    }
    vec4 texel = zodarSampleArray(array, floatBitsToUint(m.custom0.y), vec3(uv, layer));

    ZodarPbrSurface s;
    s.position = vWorldPosition;
    s.normal = n;
    s.view = normalize(zodarFrame.cameraPosition.xyz - vWorldPosition);
    s.albedo = texel.rgb * vColor.rgb * m.baseColorFactor.rgb;
    s.metallic = 0.0;
    s.roughness = clamp(m.roughness, 0.03, 1.0);
    s.occlusion = 1.0;
    ZodarLightingResult r = zodarShadePbrDetailed(s);
    vec3 color = r.direct + r.diffuseIndirect + r.specularIndirect;
    color += texel.rgb * (1.0 - texel.a) * m.custom1.x;
    outColor = vec4(zodarApplyFrameFog(color, vWorldPosition), 1.0);
}
