#version 450
#extension GL_GOOGLE_include_directive : require
#include "zodar/surface.glsl"

// Depth prepass for alpha-masked materials: the engine alpha test, no colour output.
void main() {
    zodarBaseColor(zodarMaterial());
}
