#version 410 core
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
out vec4 fragColor;

vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUv).rgb;
    vec3 bloom = texture(uBloom, vUv).rgb;
    vec3 c = (hdr + bloom * uBloomStrength) * uExposure;
    c = aces(c);
    // Gentle vignette so the trapdoor and photon ring stay the focus.
    vec2 q = vUv * 2.0 - 1.0;
    float vig = 1.0 - 0.18 * dot(q, q);
    c *= vig;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    fragColor = vec4(c, 1.0);
}
