#version 410 core
in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uTex;
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform int uMode;          // 0 extract, 1 blur, 2 composite
uniform float uExposure;
uniform vec2 uTexel;
uniform vec2 uDirection;
uniform float uBlurScale;
uniform float uBloomStrength;
uniform int uBloomOn;

vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    if (uMode == 0) {
        vec3 c = texture(uTex, vUv).rgb * uExposure;
        float l = max(c.r, max(c.g, c.b));
        float w = smoothstep(0.75, 1.7, l);
        fragColor = vec4(c * w, 1.0);
        return;
    }
    if (uMode == 1) {
        vec2 texel = uTexel * uDirection * uBlurScale;
        vec3 s = texture(uTex, vUv).rgb * 0.227027;
        s += texture(uTex, vUv + texel * 1.0).rgb * 0.1945946;
        s += texture(uTex, vUv - texel * 1.0).rgb * 0.1945946;
        s += texture(uTex, vUv + texel * 2.0).rgb * 0.1216216;
        s += texture(uTex, vUv - texel * 2.0).rgb * 0.1216216;
        s += texture(uTex, vUv + texel * 3.0).rgb * 0.054054;
        s += texture(uTex, vUv - texel * 3.0).rgb * 0.054054;
        s += texture(uTex, vUv + texel * 4.0).rgb * 0.016216;
        s += texture(uTex, vUv - texel * 4.0).rgb * 0.016216;
        fragColor = vec4(s, 1.0);
        return;
    }
    vec3 hdr = texture(uHdr, vUv).rgb * uExposure;
    vec3 col = hdr;
    if (uBloomOn == 1) col += texture(uBloom, vUv).rgb * uBloomStrength;
    col = aces(max(col, vec3(0.0)));
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
