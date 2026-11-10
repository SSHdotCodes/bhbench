#version 410 core
// =============================================================================
//  grid.frag — the "trapdoor": the curved geometry of the equatorial plane
//  drawn as an isometrically embedded surface (Flamm's paraboloid and its Kerr
//  generalisation).  Distances measured along this surface are the true
//  proper distances of the Kerr geometry.
//
//  * rings:   equally spaced in proper (ruler) distance from the horizon
//  * spokes:  every 15 degrees of azimuth
//  * colour:  gravitational time dilation (lapse) - blue where clocks run at
//             the far-away rate, red where they nearly stop
//  * pulses:  move inwards at the speed of observers falling from rest at
//             infinity ("rain"), i.e. the river-model inflow of space
//  * marked:  event horizon (rim of the hole), ergosphere, photon orbits, ISCO
// =============================================================================
in vec3  vWorld;
in vec4  vParam;
in float vLapse;

out vec4 oColor;

uniform vec3  uCamPos;
uniform vec2  uResolution;
uniform sampler2D uSceneDepth;
uniform float uTime;
uniform float uRimR;          // outer radius of the diagram
uniform float uRh;
uniform vec2  uRph;           // equatorial photon orbits (co-, counter-rotating)
uniform float uRisco;
uniform float uRergo;         // static limit (edge of the ergoregion) in the equatorial plane
uniform float uRingSpacing;   // proper distance between rings
uniform int   uStyle;         // 0 polar (proper distance), 1 draped Cartesian
uniform float uIntensity;

const float PI = 3.14159265358979;

// anti-aliased line: 1 on lines at integer values of c
float lineAA(float c, float halfWidthPx) {
    float d = abs(fract(c - 0.5) - 0.5) / max(fwidth(c), 1e-7);
    return 1.0 - smoothstep(halfWidthPx - 0.6, halfWidthPx + 0.6, d);
}
float markAA(float v, float target, float halfWidthPx) {
    float d = abs(v - target) / max(fwidth(v), 1e-7);
    return 1.0 - smoothstep(halfWidthPx - 0.6, halfWidthPx + 0.6, d);
}

void main() {
    float r = vParam.x, phi = vParam.y, tau = vParam.z, ell = vParam.w;

    float lines;
    if (uStyle == 0) {
        float ring  = lineAA(ell / uRingSpacing, 0.9);
        float spoke = lineAA(phi * 12.0 / PI, 0.9);
        lines = max(ring, spoke);
    } else {
        float gx = lineAA(vWorld.x / 2.5, 0.9);
        float gy = lineAA(vWorld.y / 2.5, 0.9);
        lines = max(gx, gy);
    }

    // time dilation colour ramp
    vec3 cFar = vec3(0.10, 0.50, 1.00), cMid = vec3(0.55, 0.25, 1.00), cNear = vec3(1.00, 0.12, 0.10);
    float a = clamp(vLapse, 0.0, 1.0);
    vec3 base = a > 0.6 ? mix(cMid, cFar, (a - 0.6) / 0.4) : mix(cNear, cMid, a / 0.6);

    // infalling pulses: phase constant along tau(r) + t
    float ph = fract((tau + uTime) / 14.0);
    float pulse = exp(-ph * 7.0) + 0.6 * exp(-(1.0 - ph) * 40.0);

    float edge = 1.0 - smoothstep(0.72 * uRimR, uRimR, r);

    vec3 col = base * lines * (0.55 + 1.6 * pulse);
    col += base * 0.035 * (1.0 + 2.0 * pulse);                 // faint sheet

    // landmarks
    float mH   = markAA(r, uRh, 2.2);
    float mPh  = max(markAA(r, uRph.x, 1.1), markAA(r, uRph.y, 1.1));
    float mIs  = markAA(r, uRisco, 1.1);
    float mEr  = markAA(r, uRergo, 1.0);
    col += vec3(1.0, 0.10, 0.06) * mH * 3.0;
    col += vec3(0.95, 0.95, 1.0) * mEr * 0.9;
    col += vec3(1.0, 0.85, 0.25) * mPh * 1.1;
    col += vec3(0.25, 1.0, 0.45) * mIs * 1.1;

    col *= edge * uIntensity;

    // Hide what lies behind the hole or behind opaque disk gas.
    float sceneDepth = texture(uSceneDepth, gl_FragCoord.xy / uResolution).r;
    float dist = length(vWorld - uCamPos);
    float vis = 1.0 - 0.90 * smoothstep(sceneDepth * 1.0, sceneDepth * 1.03 + 0.5, dist);

    oColor = vec4(col * vis, 0.0);
}
