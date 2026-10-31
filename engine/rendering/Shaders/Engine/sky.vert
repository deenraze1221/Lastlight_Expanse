#version 450
// Fullscreen triangle on the far plane (reverse Z: depth 0); with GREATER_OR_EQUAL against the
// cleared depth it only covers pixels where no geometry was drawn.
layout(location = 0) out vec2 vNdc;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
