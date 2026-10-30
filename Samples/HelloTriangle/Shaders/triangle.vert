#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
} pc;

layout(location = 0) out vec3 outColor;

// Vertex data is generated in the shader; vertex buffers arrive in Milestone 2.
const vec2 kPositions[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.45), vec2(-0.6, 0.45));
const vec3 kColors[3] = vec3[](vec3(1.0, 0.18, 0.12), vec3(0.15, 0.85, 0.25), vec3(0.2, 0.35, 1.0));

void main() {
    float c = cos(pc.time * 0.5);
    float s = sin(pc.time * 0.5);
    vec2 p = mat2(c, s, -s, c) * kPositions[gl_VertexIndex];
    p.x /= pc.aspect; // Keep the triangle undistorted for any window shape.
    gl_Position = vec4(p, 0.0, 1.0);
    outColor = kColors[gl_VertexIndex];
}
