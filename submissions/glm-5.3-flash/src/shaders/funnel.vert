#version 410 core
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
out vec3 vWorld;
void main(){
    vUV = aUV;
    vWorld = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
