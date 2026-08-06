#version 410 core
// Tone mapping and display encode.

in vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform int   uTonemap;     // 0 = asinh (astronomical), 1 = ACES filmic, 2 = linear clip
uniform float uAsinh;       // compression strength for mode 0
uniform float uVignette;

// ACES filmic approximation (Narkowicz 2015) in RRT/ODT-fitted RGB space.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Inverse-hyperbolic-sine stretch, the standard way astronomers display images
// whose surface brightness spans several decades.  A Novikov-Thorne disk runs
// about 1000:1 from the ISCO to the outer edge, which no linear or filmic curve
// can show at once.  Luminance is compressed; hue and saturation are preserved.
vec3 asinhTonemap(vec3 c, float k) {
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (L <= 1e-8) return vec3(0.0);
    float Lc = asinh(L * k) / asinh(k);
    return clamp(c * (Lc / L), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    hdr *= uExposure;

    vec3 col = (uTonemap == 0) ? asinhTonemap(hdr, uAsinh)
             : (uTonemap == 1) ? aces(hdr)
                               : clamp(hdr, 0.0, 1.0);

    if (uVignette > 0.0) {
        vec2 q = vUV - 0.5;
        col *= mix(1.0, smoothstep(0.95, 0.25, length(q)), uVignette);
    }

    // linear -> sRGB
    col = mix(col * 12.92, 1.055 * pow(max(col, 1e-5), vec3(1.0 / 2.4)) - 0.055,
              step(vec3(0.0031308), col));
    fragColor = vec4(col, 1.0);
}
