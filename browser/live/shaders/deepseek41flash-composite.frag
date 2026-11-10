#version 300 es
precision highp float;
precision highp int;

// Final composite: HDR + bloom -> exposure -> tone map -> sRGB -> dither.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uGamma;
uniform float uVignette;
uniform float uTime;
uniform vec2  uRes;

vec3 acesFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92,
               1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), c));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;
    vec3 col = hdr + bloom * uBloomStrength;

    col *= uExposure;

    // Optional power-law compression: reveals faint outer-disk structure when
    // rendering absolute blackbody radiance (uGamma > 1).
    if (uGamma > 1.001) {
        col = pow(max(col, vec3(0.0)), vec3(1.0 / uGamma));
    }

    col = acesFilm(col);

    // Vignette
    vec2 q = vUV - 0.5;
    float vig = 1.0 - uVignette * dot(q, q) * 1.6;
    col *= clamp(vig, 0.0, 1.0);

    col = linearToSRGB(col);

    // Ordered-ish dither to kill banding in the dark sky.
    float d = hash12(gl_FragCoord.xy + fract(uTime) * 137.0) - 0.5;
    col += d / 255.0;

    fragColor = vec4(col, 1.0);
}
