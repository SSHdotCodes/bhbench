#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    vNormal = inNormal;
    gl_Position = uViewProj * vec4(inPos, 1.0);
}
