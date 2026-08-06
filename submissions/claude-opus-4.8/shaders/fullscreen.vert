#version 410 core
// Emits one big triangle that covers the whole screen. No vertex buffer needed;
// positions come from gl_VertexID. Used by the ray-tracer and the blit/bloom pass.
void main() {
    vec2 verts[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);
}
