#version 410 core

layout(location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;

out float vDepth;
out float vRadius;

void main() {
    vec4 world = vec4(aPos, 1.0);
    vec4 clip = uProj * uView * world;
    gl_Position = clip;
    vDepth = aPos.y;
    vRadius = length(vec2(aPos.x, aPos.z));
}