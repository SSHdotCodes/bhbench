#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uRs;
out vec3 vPos;
out float vDepth;
void main() {
    vPos = aPos;
    float r = length(aPos.xz);
    vDepth = (r - uRs) / (20.0 - uRs);
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
