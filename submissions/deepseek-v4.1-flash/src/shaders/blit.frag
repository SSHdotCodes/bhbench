#version 410 core

// Simple blit used for the temporal accumulation resolve and for copying.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSrc;
uniform float uWeight;   // blend weight of the new sample

void main() {
    vec3 c = texture(uSrc, vUV).rgb;
    fragColor = vec4(c, uWeight);
}
