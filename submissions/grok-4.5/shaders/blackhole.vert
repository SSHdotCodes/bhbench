#version 410 core

// Full-screen triangle (no VBO data needed beyond gl_VertexID).
const vec2 verts[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

out vec2 vUV;

void main() {
    vec2 p = verts[gl_VertexID];
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
