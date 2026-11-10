#version 300 es
precision highp float;
precision highp int;
uniform vec3 uRingColor;
uniform float uRingAlpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(uRingColor * uRingAlpha, 1.0);
}
