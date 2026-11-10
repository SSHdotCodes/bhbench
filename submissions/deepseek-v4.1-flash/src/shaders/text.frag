#version 410 core

in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D uAtlas;

void main() {
    float a = texture(uAtlas, vUV).r;
    if (a < 0.5) discard;
    fragColor = vec4(vColor.rgb, vColor.a);
}
