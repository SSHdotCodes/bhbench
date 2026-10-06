#version 410 core
// Fullscreen triangle: aPos in [-1, 3], reused by every screen-space pass.
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
