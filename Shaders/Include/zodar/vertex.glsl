// Standard engine vertex inputs/outputs for zodar::Vertex. Custom vertex shaders can
// include this and modify positions (e.g. snapping, wind, deformation) before writing
// gl_Position, while keeping the same varyings for engine fragment shaders.
#ifndef ZODAR_VERTEX_GLSL
#define ZODAR_VERTEX_GLSL

#include "zodar/scene.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv0;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUv0;
layout(location = 4) out vec4 vColor;

// Writes all varyings and returns the clip-space position (not yet assigned).
vec4 zodarTransformVertex() {
    ZodarInstance instance = zodarInstances[gl_InstanceIndex];
    vec4 world = instance.model * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = normalize(mat3(instance.normalMatrix) * inNormal);
    vTangent = vec4(normalize(mat3(instance.model) * inTangent.xyz), inTangent.w);
    vUv0 = inUv0;
    vColor = inColor;
    return zodarFrame.viewProjection * world;
}

#endif
