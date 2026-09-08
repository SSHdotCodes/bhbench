#version 410 core
// Fullscreen-triangle vertex shader (no VBO needed; uses gl_VertexID).
out vec2 vNDC;

void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    vNDC = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
