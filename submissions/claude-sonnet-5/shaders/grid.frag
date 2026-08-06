#version 410 core

in float vR;
in float vY;

uniform float uRs;
uniform float uDiskInner;
uniform float uDiskOuter;

out vec4 FragColor;

void main() {
    vec3 color;
    if (vR <= uRs * 1.02) {
        color = vec3(1.0, 0.25, 0.05); // event horizon: the throat of the funnel
    } else if (vR >= uDiskInner && vR <= uDiskOuter) {
        float t = clamp((vR - uDiskInner) / max(uDiskOuter - uDiskInner, 1e-4), 0.0, 1.0);
        color = mix(vec3(1.0, 0.55, 0.1), vec3(0.3, 0.4, 0.8), t); // marks where the accretion disk sits
    } else {
        float fade = clamp(1.0 - (vR - uDiskOuter) / (uDiskOuter * 3.0), 0.2, 1.0);
        color = vec3(0.35, 0.55, 0.78) * fade;
    }
    FragColor = vec4(color, 1.0);
}
