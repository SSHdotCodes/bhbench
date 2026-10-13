#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;
uniform float     uBloomStrength;
uniform float     uExposure;

vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdrScene = texture(uSceneTex, vUV).rgb;
    vec3 hdrBloom = texture(uBloomTex, vUV).rgb;

    vec3 combined = hdrScene + hdrBloom * uBloomStrength;
    combined *= uExposure;

    vec3 mapped = acesFilm(combined);
    vec2 centered = vUV * 2.0 - 1.0;
    float vig = 1.0 - 0.14 * dot(centered, centered);
    mapped *= vig;

    vec3 srgb = pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(srgb, 1.0);
}
