#version 410 core

in float vDepth;
in float vRadius;

uniform float uRs;
uniform float uTime;

out vec4 FragColor;

void main() {
    // Color encodes depth: deep blue "trapdoor" near horizon, cyan farther out.
    float depthNorm = clamp((vDepth + 4.0 * uRs) / (6.0 * uRs), 0.0, 1.0);
    vec3 nearColor = vec3(0.05, 0.15, 0.55);
    vec3 farColor  = vec3(0.1, 0.55, 0.75);
    vec3 color = mix(nearColor, farColor, depthNorm);

    // Pulsing highlight near event horizon to emphasize curvature.
    float horizonGlow = exp(-abs(vRadius - uRs * 1.5) * 0.8);
    color += vec3(0.15, 0.35, 0.9) * horizonGlow * (0.5 + 0.5 * sin(uTime * 1.5));

    float alpha = 0.35 + 0.25 * depthNorm;
    FragColor = vec4(color, alpha);
}