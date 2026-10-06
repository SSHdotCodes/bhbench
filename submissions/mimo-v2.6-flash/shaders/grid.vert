#version 410 core
// Flamm-paraboloid spacetime sheet.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aCurv;

uniform mat4 uMVP;

out vec3 vNormal;
out vec3 vWorld;
out float vCurv;

void main() {
    vNormal = aNormal;
    vWorld = aPos;
    vCurv = aCurv;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
