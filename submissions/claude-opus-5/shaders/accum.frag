#version 410 core
// Progressive accumulation: while the camera is still we keep averaging jittered
// samples, which both anti-aliases the photon ring and cleans up the star field.

in vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uNew;
uniform sampler2D uHistory;
uniform float uWeight;    // 1/(n+1)

void main() {
    vec3 n = texture(uNew, vUV).rgb;
    vec3 o = texture(uHistory, vUV).rgb;
    fragColor = vec4(mix(o, n, uWeight), 1.0);
}
