#pragma once

// Fullscreen-triangle vertex shader (no attributes, uses gl_VertexID).
inline const char* VS_FULL = R"GLSL(
#version 330 core
void main() {
    vec2 p[3];
    p[0] = vec2(-1.0, -1.0);
    p[1] = vec2( 3.0, -1.0);
    p[2] = vec2(-1.0,  3.0);
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Ray-traced black hole pass.
//
// Physics notes (units: Schwarzschild radius Rs = 1, so GM = 0.5):
//   * Null geodesics in Schwarzschild geometry, written in Cartesian form,
//     obey   d2x/dl2 = -(3/2) * Rs * h2 * x / r^5,  h2 = |x x v|^2 = const
//     (equivalent to the Binet equation u'' + u = 3GM u^2 that produces
//     light bending, the photon sphere at r = 1.5 Rs and the shadow of
//     radius b_crit = 3*sqrt(3)*GM = 2.598 Rs).
//   * Event horizon at r = Rs = 1. Photon capture -> black.
//   * Accretion disk between ISCO (r = 3 Rs = 6GM, the innermost stable
//     circular orbit) and an outer rim, with a Shakura-Sunyaev temperature
//     profile T ~ r^-3/4 (1 - sqrt(r_in/r))^1/4, Keplerian orbital speed
//     v = sqrt(GM/(r - Rs)) (the exact local circular-orbit speed), special
//     relativistic Doppler boosting 1/(gamma (1 - beta.n)), gravitational
//     redshift sqrt(1 - Rs/r), and intensity beaming ~ g^3.
//   * Spacetime grid: Flamm's paraboloid, the embedding diagram of the
//     Schwarzschild spatial geometry, z(r) = 2*sqrt(Rs*(r - Rs)) — the
//     "trapdoor". It is ray-marched through the *same* geodesic integrator,
//     so the grid itself is gravitationally lensed.
// ---------------------------------------------------------------------------
inline const char* FS_RAY = R"GLSL(
#version 330 core

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uFovScale;
uniform float uTime;
uniform int   uLens;     // gravitational bending + capture (GR on/off)
uniform int   uDisk;
uniform int   uGrid;
uniform int   uStars;

out vec4 fragColor;

const float RS       = 1.0;      // Schwarzschild radius
const float GM       = 0.5;      // G*M in these units
const float DISK_IN  = 3.0;      // ISCO = 6GM = 3 Rs
const float DISK_OUT = 14.0;
const float GRID_R   = 10.0;
const float B_CRIT   = 2.5980762; // 3*sqrt(3)*GM : photon-sphere impact param
const int   MAX_STEPS = 430;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i),            hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.13 + 17.7; a *= 0.5; }
    return s;
}

// Flamm's paraboloid (lower sheet): the funnel of curved space.
float funnelY(float r) {
    if (uLens == 0) return 0.0;
    float z = -2.0 * sqrt(max(r - RS, 0.0));      // z = 2*sqrt(Rs(r-Rs))
    return z * smoothstep(GRID_R, GRID_R * 0.55, r);
}

// Planck-locus blackbody color (approximation of Tanner Helland's fit).
vec3 blackbody(float kT) {
    float t = clamp(kT, 1200.0, 20000.0) / 100.0;
    float r, g, b;
    if (t <= 66.0) {
        r = 255.0;
        g = clamp(99.4708025861 * log(t) - 161.1195681661, 0.0, 255.0);
        b = (t <= 19.0) ? 0.0 : clamp(138.5177312231 * log(t - 10.0) - 305.0447927307, 0.0, 255.0);
    } else {
        r = clamp(329.698727446 * pow(t - 60.0, -0.1332047592), 0.0, 255.0);
        g = clamp(288.1221695283 * pow(t - 60.0, -0.0755148492), 0.0, 255.0);
        b = 255.0;
    }
    return vec3(r, g, b) / 255.0;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float lineMask(float coord) {
    float f = fract(coord);
    float d = min(f, 1.0 - f);
    return 1.0 - smoothstep(0.0, 0.045, d);
}

vec3 starField(vec3 d) {
    vec3 col = vec3(0.0);
    vec3 ad = abs(d);
    vec2 uv; float face;
    if (ad.x > ad.y && ad.x > ad.z)      { face = 0.0; uv = vec2(d.z, d.y) / ad.x; }
    else if (ad.y > ad.z)                { face = 1.0; uv = vec2(d.x, d.z) / ad.y; }
    else                               { face = 2.0; uv = vec2(d.x, d.y) / ad.z; }
    uv = uv * 0.5 + 0.5;

    // galactic band + faint nebula
    float band = pow(max(0.0, 1.0 - abs(dot(d, normalize(vec3(0.30, 0.35, 0.20))))), 3.0);
    col += vec3(0.016, 0.026, 0.050) * fbm(d.xz * 2.5 + face * 3.0 + d.y * 1.7)
           * (0.35 + band);

    for (int layer = 0; layer < 2; layer++) {
        float n = (layer == 0) ? 150.0 : 330.0;
        vec2 g = uv * n + face * 137.0 + layer * 61.0;
        vec2 cell = floor(g);
        vec2 fp = fract(g) - 0.5;
        vec3 h = hash33(vec3(cell, face * 31.0 + layer * 7.0));
        float present = step(1.0 - 0.016 * (1.0 + band * 7.0), h.x);
        float sz = 0.12 + 0.5 * h.z * h.z;
        float s = pow(max(0.0, 1.0 - length(fp) / sz), 14.0) * present;
        float tw = 0.78 + 0.22 * sin(uTime * (1.5 + 6.0 * h.y) + h.z * 40.0);
        vec3 sc = mix(vec3(1.0, 0.72, 0.52), vec3(0.62, 0.78, 1.0), h.y);
        col += s * tw * sc * (0.5 + h.z) * (1.0 + band * 2.5);
    }
    return col;
}

// Relativistic accretion-disk emission at a plane crossing point hp.
void diskEmission(vec3 hp, vec3 vdir, inout vec3 col, inout float trans) {
    float r = length(hp.xz);
    if (r < DISK_IN || r > DISK_OUT || trans < 0.02) return;

    // local physical circular-orbit speed and direction (clockwise)
    float beta = clamp(sqrt(GM / max(r - RS, 0.05)), 0.0, 0.95);
    float gamma = 1.0 / sqrt(max(1.0 - beta * beta, 1e-4));
    vec3 tang = normalize(vec3(hp.z, 0.0, -hp.x));
    vec3 bvec = beta * tang;
    // photon direction of travel ~= ray direction vdir
    float dop = 1.0 / (gamma * max(1.0 - dot(bvec, normalize(vdir)), 0.05));
    dop *= 1.0 / sqrt(max(1.0 - RS / r, 0.02));        // gravitational redshift
    dop = clamp(dop, 0.12, 8.0);

    // Shakura-Sunyaev effective temperature
    float temp = 5200.0 * pow(DISK_IN / r, 0.75)
               * pow(max(1.0 - sqrt(DISK_IN / r), 0.0), 0.25) * dop;

    // Keplerian-sheared turbulence  (Omega ~ r^-3/2)
    float phi = atan(hp.z, hp.x);
    float omega = 11.2 * pow(r, -1.5);
    vec2 uv = vec2(log(r) * 4.0,
                   phi * 2.2 + uTime * omega * 1.1
                       + fbm(vec2(log(r) * 3.0, phi)) * 1.6);
    float n = vnoise(uv * 3.0) * 0.62 + vnoise(uv * 8.0 + 11.3) * 0.38;
    float dens = smoothstep(0.06, 0.65, n);
    float alpha = clamp((0.22 + 0.95 * dens) * (1.0 - r / DISK_OUT) + 0.05, 0.0, 1.0);

    vec3 emission = blackbody(temp) * pow(temp / 3000.0, 3.0) * 0.11 * (0.5 + 1.3 * dens);
    col   += trans * alpha * emission;
    trans *= (1.0 - alpha);
}

// Lensed spacetime-grid line at hit point hp.
void gridEmission(vec3 hp, inout vec3 col, inout float trans) {
    float r = length(hp.xz);
    if (r < RS || r > GRID_R || trans < 0.02) return;
    float theta = atan(hp.z, hp.x);
    float mask = max(lineMask(r * 2.0), lineMask(theta * 5.729578) * 0.9);
    float fade = smoothstep(GRID_R, GRID_R * 0.6, r);
    float depthT = clamp((hp.y + 4.0) / 4.0, 0.0, 1.0);   // 0 deep in the throat
    vec3 lc = mix(vec3(1.5, 0.45, 0.12), vec3(0.14, 0.62, 1.15), depthT);
    lc += vec3(0.7, 0.32, 0.10) * smoothstep(1.45, 1.02, r); // glowing horizon rim
    float a = mask * fade * 0.9;
    col   += trans * a * lc;
    trans *= (1.0 - a * 0.9);
}

void main() {
    vec2 ndc = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uFovScale
                                  + uCamUp    * ndc.y * uFovScale);

    vec3 pos = uCamPos;
    vec3 vel = dir;
    vec3 hm  = cross(pos, vel);
    float h2 = dot(hm, hm);                       // conserved along the geodesic

    vec3 col = vec3(0.0);
    float trans = 1.0;
    bool escaped = false;
    int diskHits = 0, gridHits = 0;
    float fPrev = pos.y - funnelY(length(pos.xz));

    for (int i = 0; i < MAX_STEPS; i++) {
        float r = length(pos);
        if (uLens == 1 && r < RS) break;   // photon captured by the horizon
        if (r > 70.0 && dot(pos, vel) > 0.0) { escaped = true; break; }
        if (trans < 0.02 && uStars == 0) break;

        float dt = (uLens == 1) ? clamp(0.12 * (r - RS), 0.008, 0.55) : 0.5;

        vec3 acc = (uLens == 1)
                 ? -1.5 * h2 * pos / pow(dot(pos, pos), 2.5)
                 : vec3(0.0);
        vel += acc * (0.5 * dt);
        vec3 npos = pos + vel * dt;
        vec3 acc2 = (uLens == 1)
                  ? -1.5 * h2 * npos / pow(dot(npos, npos), 2.5)
                  : vec3(0.0);
        vel += acc2 * (0.5 * dt);

        // accretion disk: equatorial-plane (y = 0) crossing
        if (uDisk == 1 && diskHits < 6 && pos.y * npos.y < 0.0) {
            float s = pos.y / (pos.y - npos.y);
            diskEmission(mix(pos, npos, s), vel, col, trans);
            diskHits++;
        }
        // spacetime grid (Flamm paraboloid surface) crossing
        float fCur = npos.y - funnelY(length(npos.xz));
        if (uGrid == 1 && gridHits < 24 && fPrev * fCur < 0.0) {
            float s = fPrev / (fPrev - fCur);
            gridEmission(mix(pos, npos, s), col, trans);
            gridHits++;
        }
        fPrev = fCur;
        pos = npos;
    }

    if (escaped) {
        if (uStars == 1) col += trans * starField(normalize(vel));
        // photon-ring halo near the critical impact parameter
        if (uLens == 1) {
            float b = sqrt(h2);
            float ring  = smoothstep(0.22, 0.0, abs(b - B_CRIT));
            float halo  = smoothstep(1.1, 0.0, abs(b - B_CRIT));
            col += trans * (ring * ring * 0.10 + halo * halo * halo * 0.03)
                   * vec3(1.0, 0.85, 0.65);
        }
    }

    col *= 1.5;                                     // exposure
    col = aces(col);
    fragColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

inline const char* FS_PRESENT = R"GLSL(
#version 330 core
uniform sampler2D uTex;
uniform vec2 uWinRes;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, gl_FragCoord.xy / uWinRes);
}
)GLSL";