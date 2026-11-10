#version 410 core
// HDR composite: bloom add, exposure, ACES tonemap, gamma, vignette.
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene, uBloom;
uniform float uExposure, uBloomStrength;
vec3 aces(vec3 x) {
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(uScene, vUV).rgb + uBloomStrength * texture(uBloom, vUV).rgb;
    c *= uExposure;
    c = aces(c);
    c = pow(c, vec3(1.0/2.2));
    vec2 q = vUV - 0.5;
    c *= 1.0 - 0.25 * dot(q, q);
    fragColor = vec4(c, 1.0);
}
