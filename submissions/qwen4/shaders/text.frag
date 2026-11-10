#version 410 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uAtlas;
uniform vec4 uColor;
void main() {
    float a = texture(uAtlas, vUV).r;
    if (a < 0.5) discard;
    fragColor = uColor;
}
