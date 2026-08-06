#version 410 core
// Colors the embedding grid by the lapse sqrt(1 - 2M/r): white-blue far out
// where clocks run normally, red-hot at the throat where time freezes.
in float vR;
out vec4 fragColor;
uniform float uTime;
uniform int uKind;      // 0 = grid lines, 1 = horizon cap, 2 = particles

void main() {
    if (uKind == 1) {                         // opaque throat cap
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (uKind == 2) {                         // orbiting test masses
        vec2 d = gl_PointCoord - 0.5;
        float m = smoothstep(0.5, 0.15, length(d));
        fragColor = vec4(vec3(1.0, 0.85, 0.55) * 6.0 * m, 1.0);
        return;
    }
    float lapse = sqrt(max(1.0 - 2.0 / vR, 0.0));
    vec3 c = mix(vec3(1.00, 0.22, 0.05), vec3(0.30, 0.60, 1.00),
                 smoothstep(0.0, 0.9, lapse));
    float I = 0.30 + 3.0 * pow(1.0 - lapse, 3.0);

    // accretion-disk band painted onto the embedding surface
    float band = smoothstep(5.4, 6.2, vR) * (1.0 - smoothstep(14.0, 17.0, vR));
    c = mix(c, vec3(1.0, 0.62, 0.22), band * 0.85);
    I += band * 1.3;

    // slow inward-traveling pulse, hinting at the inexorable infall
    I *= 0.85 + 0.30 * sin(vR * 1.5 + uTime * 1.8);

    fragColor = vec4(c * I, 1.0);
}
