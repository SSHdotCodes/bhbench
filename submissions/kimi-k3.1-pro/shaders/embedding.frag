#version 330 core

in float vDepth;
in vec3 vColor;
out vec4 fragColor;
uniform float uAlpha;

void main() {
    float fog = clamp(1.15 - vDepth / 150.0, 0.35, 1.0);
    fragColor = vec4(vColor * fog, uAlpha);
}
