#version 330 core
in vec3 vPosition;
in vec3 vNormal;
uniform float uTime;
uniform vec3 uBlackHolePos;
uniform float uSchwarzschildRadius;
out vec4 FragColor;

void main() {
    float r = length(vPosition - uBlackHolePos);
    float curvature = exp(-r / (2.0 * uSchwarzschildRadius + 0.5)) * 3.0;
    float gridX = sin(vPosition.x * 3.0 + uTime * 0.1) * 0.5 + 0.5;
    float gridZ = sin(vPosition.z * 3.0 + uTime * 0.05) * 0.5 + 0.5;
    float grid = smoothstep(0.45, 0.55, max(gridX, gridZ));
    vec3 baseColor = vec3(0.02, 0.01, 0.08);
    vec3 glow = vec3(0.3, 0.1, 0.6) * curvature * 0.6;
    vec3 finalColor = mix(baseColor + glow, vec3(0.95, 0.95, 1.0), grid);
    FragColor = vec4(finalColor, 1.0);
}
