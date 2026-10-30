#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven/vertex.glsl"

void main() {
    gl_Position = ravenTransformVertex();
}
