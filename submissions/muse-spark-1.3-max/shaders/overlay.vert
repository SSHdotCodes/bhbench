#version 410 core
// Pixel-space overlay (text + legend boxes).
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uResolution;
out vec4 vCol;
void main() {
    vec2 ndc = aPos / uResolution * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vCol = aCol;
}
