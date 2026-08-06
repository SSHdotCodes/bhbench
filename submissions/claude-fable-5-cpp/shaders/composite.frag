#version 410 core
// Final composite: scene + bloom halos, ACES tonemap, gamma, vignette, dither.
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uDither;   // 0 when recording video: animated grain poisons encoders

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 c = texture(uScene, vUV).rgb + uBloomStrength * texture(uBloom, vUV).rgb;
    c *= uExposure;
    c = aces(c);
    c = pow(c, vec3(1.0 / 2.2));
    c *= 1.0 - 0.30 * pow(length(vUV - 0.5) * 1.30, 3.0);
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0 * uDither;
    fragColor = vec4(c, 1.0);
}
