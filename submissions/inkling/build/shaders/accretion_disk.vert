#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform float uTime;
uniform vec3 uBlackHolePos;
uniform float uSchwarzschildRadius;
out vec3 vPosition;
out vec3 vNormal;
void main() {
    vPosition = aPos;
    vNormal = vec3(0.0, 1.0, 0.0);
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
