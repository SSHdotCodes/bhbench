#version 410 core

// Spacetime grid / Flamm paraboloid visualization — the classic
// "trapdoor in spacetime" embedding of the Schwarzschild equatorial geometry.

in vec3 vWorldPos;
in vec3 vNormal;
in float vRadius;

out vec4 fragColor;

uniform vec3 uCamPos;
uniform float uRs;
uniform float uTime;
uniform int uMode; // 0 = surface, 1 = lines, 2 = cage

void main() {
    float r = max(vRadius, uRs * 1.01);
    // Embedding depth factor (how deep into the throat)
    float depth = 2.0 * sqrt(max(uRs * (r - uRs), 0.0));
    float throat = smoothstep(uRs * 6.0, uRs * 1.1, r);

    vec3 base;
    if (uMode == 0) {
        // Translucent surface: cyan → deep purple into the throat
        base = mix(vec3(0.15, 0.55, 0.75), vec3(0.35, 0.05, 0.55), throat);
        float fres = pow(1.0 - abs(dot(normalize(uCamPos - vWorldPos), normalize(vNormal))), 2.0);
        base += fres * vec3(0.4, 0.7, 1.0) * 0.35;
        // Pulse along radial waves (aesthetic, not physical)
        float wave = 0.5 + 0.5 * sin(r * 1.2 - uTime * 1.5);
        base += wave * 0.05 * vec3(0.5, 0.8, 1.0);
        float alpha = mix(0.18, 0.55, throat);
        fragColor = vec4(base, alpha);
    } else if (uMode == 1) {
        // Grid lines on Flamm surface
        base = mix(vec3(0.3, 0.9, 1.0), vec3(1.0, 0.4, 0.9), throat);
        float pulse = 0.75 + 0.25 * sin(uTime * 2.0 + r);
        fragColor = vec4(base * pulse, mix(0.35, 0.95, throat));
    } else {
        // Curvature cage
        base = mix(vec3(0.2, 1.0, 0.55), vec3(1.0, 0.5, 0.1), throat);
        fragColor = vec4(base, mix(0.25, 0.85, throat));
    }
}
