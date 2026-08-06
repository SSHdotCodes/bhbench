#version 330 core
in float vDist;
in vec3 vWorldPos;
out vec4 FragColor;

uniform float uRs;
uniform float uAlpha;

void main() {
    // Fade with distance and near horizon
    float r = length(vWorldPos.xy);
    float fade = exp(-vDist * 0.008) * smoothstep(uRs * 0.7, uRs * 1.5, r);
    float intensity = 0.85 * fade;
    vec3 col = vec3(0.55, 0.75, 1.0) * intensity; // cyan-ish grid for sci-fi accurate look
    FragColor = vec4(col, uAlpha * fade * 0.9);
}
