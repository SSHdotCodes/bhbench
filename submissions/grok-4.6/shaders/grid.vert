#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProj;
out vec3 vColor;
out vec3 vWorld;
void main() {
    vColor = aColor;
    vWorld = aPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
