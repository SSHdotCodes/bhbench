#version 410 core
// Fullscreen triangle vertex shader. ndcPos covers the whole viewport.
layout(location = 0) in vec2 ndcPos;
out vec2 vUV;
void main() {
    vUV = ndcPos * 0.5 + 0.5;
    gl_Position = vec4(ndcPos, 0.0, 1.0);
}
