#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;
uniform float uPointSize;

out vec4 vColor;
out vec3 vWorld;

void main() {
    vColor = aColor;
    vWorld = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
