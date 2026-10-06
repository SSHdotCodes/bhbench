#version 410 core
// Bright pass + 4x box downsample for the bloom chain.
in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uTex;
uniform float uThreshold;

void main() {
    vec2 t = 1.0 / vec2(textureSize(uTex, 0));
    vec3 c = texture(uTex, vUV + t * vec2(-1.0, -1.0)).rgb;
    c += texture(uTex, vUV + t * vec2(1.0, -1.0)).rgb;
    c += texture(uTex, vUV + t * vec2(-1.0, 1.0)).rgb;
    c += texture(uTex, vUV + t * vec2(1.0, 1.0)).rgb;
    c *= 0.25;
    float l = max(max(c.r, c.g), c.b);
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
    outColor = vec4(c * k, 1.0);
}
