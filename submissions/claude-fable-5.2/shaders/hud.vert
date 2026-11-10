#version 410 core
out vec2 vUV;
uniform vec4 uRect;     // x, y, w, h in NDC
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vUV = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(uRect.xy + p * uRect.zw, 0.0, 1.0);
}
