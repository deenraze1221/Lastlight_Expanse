// Fullscreen triangle helper: draw 3 vertices without vertex input.
#ifndef ZODAR_FULLSCREEN_GLSL
#define ZODAR_FULLSCREEN_GLSL

// Returns clip position and writes uv in [0,1] with (0,0) at the top-left.
vec4 zodarFullscreenTriangle(uint vertexIndex, out vec2 uv) {
    uv = vec2((vertexIndex << 1) & 2, vertexIndex & 2);
    return vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#endif
