#version 410 core
// Flamm-paraboloid embedding view ("trapdoor in spacetime").
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aR;   // Schwarzschild r of this vertex
uniform mat4 uMVP;
uniform float uPointSize;
out float vR;
void main() {
    vR = aR;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
