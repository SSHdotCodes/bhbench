#version 410 core
layout(location=0)in vec3 position;
uniform mat4 mvp;out vec3 world;
void main(){world=position;gl_Position=mvp*vec4(position,1.);}
