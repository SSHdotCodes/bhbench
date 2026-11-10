#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 inPos;    // world-space point on the embedding surface
layout(location = 1) in float inDepth; // 0 at the rim .. 1 at the throat
uniform mat4 uViewProj;
out float vDepth;
void main() {
    vDepth = inDepth;
    gl_Position = uViewProj * vec4(inPos, 1.0);
}
