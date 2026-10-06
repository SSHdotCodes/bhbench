#version 410 core
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
uniform mat4 mvp;
uniform float pointSize;
out vec4 tint;
void main() { gl_Position=mvp*vec4(position,1); tint=color; gl_PointSize=pointSize; }
