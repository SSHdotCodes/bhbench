#version 410 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;     // premultiplied alpha
uniform float uOpacity;
void main() { oColor = texture(uTex, vUV) * uOpacity; }
