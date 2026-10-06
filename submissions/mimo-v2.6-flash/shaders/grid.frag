#version 410 core
//
// Curvature-shaded surface for the "trapdoor in spacetime" sheet.
// vCurv is log-normalised |K| with K = -rs/(2 r^3), the Gaussian curvature of
// the t = const Schwarzschild slice (identical for the Flamm embedding).
//
in vec3 vNormal;
in vec3 vWorld;
in float vCurv;
layout(location = 0) out vec4 outColor;

uniform vec3 uLightDir;
uniform vec3 uCamPos;

// indigo -> blue -> cyan -> yellow -> red
vec3 ramp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.055, 0.030, 0.170);
    vec3 c1 = vec3(0.090, 0.180, 0.560);
    vec3 c2 = vec3(0.070, 0.560, 0.560);
    vec3 c3 = vec3(0.930, 0.800, 0.180);
    vec3 c4 = vec3(1.000, 0.300, 0.180);
    float s = t * 4.0;
    if (s < 1.0) return mix(c0, c1, s);
    if (s < 2.0) return mix(c1, c2, s - 1.0);
    if (s < 3.0) return mix(c2, c3, s - 2.0);
    return mix(c3, c4, s - 3.0);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorld);
    if (dot(N, V) < 0.0) N = -N;              // two-sided sheet
    vec3 L = normalize(uLightDir);

    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);

    vec3 base = ramp(vCurv);
    vec3 col = base * (0.22 + 0.95 * diff);
    col += spec * vec3(0.55, 0.68, 0.95) * 0.45;
    col += rim * base * 0.35;
    col *= 0.92;

    outColor = vec4(pow(max(col, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
