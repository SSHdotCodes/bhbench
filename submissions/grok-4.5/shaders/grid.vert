#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uView;
uniform float uTime;
uniform float uRs;

out vec3 vWorldPos;
out vec3 vNormal;
out float vRadius;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(transpose(inverse(mat3(uModel)))) * aNormal;
    vRadius = length(vec2(aPos.x, aPos.z));
    gl_Position = uMVP * vec4(aPos, 1.0);
}
