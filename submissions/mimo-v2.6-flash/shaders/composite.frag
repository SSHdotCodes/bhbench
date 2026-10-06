#version 410 core
// HDR composite: bloom add, exposure, ACES filmic tone map, sRGB encode.
in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 c = texture(uHDR, vUV).rgb + texture(uBloom, vUV).rgb * uBloomStrength;
    c *= uExposure;
    c = aces(c);
    c = pow(c, vec3(1.0 / 2.2));
    outColor = vec4(c, 1.0);
}
