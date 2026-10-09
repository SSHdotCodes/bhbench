#version 330 core
// Fullscreen triangle with UV (gl_VertexID driven, no vertex buffers).
out vec2 vUV;

void main()
{
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
