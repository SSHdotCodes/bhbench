#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out float vDepth;
out vec3 vColor;

void main() {
    vec4 clip = uMVP * vec4(aPosition, 1.0);
    gl_Position = clip;
    vDepth = clip.w;
    vColor = aColor;
}
