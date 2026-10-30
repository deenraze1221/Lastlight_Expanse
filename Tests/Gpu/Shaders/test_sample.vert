#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/fullscreen.glsl"

layout(location = 0) out vec2 outUv;

void main() {
    gl_Position = zodarFullscreenTriangle(gl_VertexIndex, outUv);
}
