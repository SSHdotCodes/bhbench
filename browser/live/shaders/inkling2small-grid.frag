#version 300 es
precision highp float;
precision highp int;
uniform float uTime;
uniform float uGridAlpha;
in float vDepth;
out vec4 fragColor;
void main() {
    // weak curvature: glacial steel blue .. strong: ember orange-red (throat)
    vec3 weak   = vec3(0.32, 0.72, 1.00);
    vec3 strong = vec3(1.00, 0.38, 0.13);
    vec3 col    = mix(weak, strong, pow(clamp(vDepth, 0.0, 1.0), 1.7));
    // Subtle metronomic "wave in" shimmer — a visual aid, not a physical wave
    float pulse = 0.78 + 0.22 * sin(uTime * 1.6 - vDepth * 8.0);
    fragColor = vec4(col * pulse * uGridAlpha, 1.0);
}
