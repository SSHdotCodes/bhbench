// shaders.h — GLSL 410 (core) sources for the black hole ray tracer.
//
// Physics summary (geometrized units G = c = M = 1, so lengths are in units of M = GM/c^2):
//   * Photons follow null geodesics of the Schwarzschild metric
//         ds^2 = -(1 - 2M/r) dt^2 + (1 - 2M/r)^{-1} dr^2 + r^2 dOmega^2 .
//     Exact orbits are integrated with the Binet equation (exact for Schwarzschild):
//         d^2u/dphi^2 + u = 3 M u^2 ,  u = 1/r ,
//     using a classical RK4 integrator with a step size that refines automatically
//     near the photon sphere (r = 3M) where d^2u/dphi^2 is stiff and orbits wind.
//   * The camera ray defines an orbital plane; the 3-D position is reconstructed as
//     p(phi) = (cos phi * e1 + sin phi * e2) / u, which lets us detect exact
//     intersections with the equatorial accretion disk (y = 0 plane).
//   * Disk emission: Novikov-Thorne temperature profile, Keplerian rotation
//     (Omega = sqrt(M/r^3)), local beta = sqrt(M/(r-2M)) as measured by a static
//     observer, Lorentz factor gamma, gravitational redshift sqrt(1-2M/r), total
//     frequency shift g = sqrt(1-2M/r) / (gamma (1 - beta cos theta)),
//     observed specific intensity boosted by g^3.
//   * Capture condition r <= 2M (event horizon), photon sphere at r = 3M,
//     critical impact parameter b_crit = 3*sqrt(3) M (shadow radius),
//     ISCO at r = 6M (inner edge of the disk for a non-spinning BH).
#pragma once

namespace shaders {

static const char* kQuadVert = R"GLSL(
#version 410 core
layout(location = 0) in vec2 inPos;   // fullscreen triangle in clip space
void main() {
    gl_Position = vec4(inPos, 0.9995, 1.0);
}
)GLSL";

static const char* kRaytraceFrag = R"GLSL(
#version 410 core

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uTime;
uniform float uExposure;

uniform int   uLensing;       // 1 = exact Schwarzschild geodesics, 0 = straight lines
uniform int   uDiskOn;
uniform int   uStarsOn;
uniform float uHaloStrength;  // 0 = off, ~0.8 default, ~1.6 strong
uniform float uDiskInner;     // 6.0  (ISCO)
uniform float uDiskOuter;     // 30.0
uniform float uDiskPeakTemp;  // Kelvin, temperature at the peak of the NT profile
uniform float uDiskIntensity; // overall brightness multiplier
uniform float uDiskSwirl;     // 0 = smooth disk, 1 = full MHD-ish turbulence
uniform int   uDebug;         // 0 normal, 1 temp, 2 radius, 3 g-factor, 4 boost, 5 structure

out vec4 fragColor;

vec3 gDebug = vec3(0.0);

const float PI = 3.14159265358979;
const float R_HORIZON = 2.0;   // r_s = 2M
const float R_PHOTON  = 3.0;   // photon sphere
const float R_ESCAPE  = 350.0; // integrate until r > R_ESCAPE (deflection remainder < 0.01 rad)
const int   MAX_STEPS = 1500;

// ---------------------------------------------------------------- utilities
float hash13(vec3 p) {
    p = fract(p * 443.8975);
    p += dot(p, p.zxy + 19.19);
    return fract((p.x + p.y) * p.z);
}

float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash13(i + vec3(0.0, 0.0, 0.0)), hash13(i + vec3(1.0, 0.0, 0.0)), u.x),
            mix(hash13(i + vec3(0.0, 1.0, 0.0)), hash13(i + vec3(1.0, 1.0, 0.0)), u.x), u.y),
        mix(mix(hash13(i + vec3(0.0, 0.0, 1.0)), hash13(i + vec3(1.0, 0.0, 1.0)), u.x),
            mix(hash13(i + vec3(0.0, 1.0, 1.0)), hash13(i + vec3(1.0, 1.0, 1.0)), u.x), u.y),
        u.z);
}

float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += a * vnoise3(p);
        p = p * 2.03 + vec3(17.3, 9.1, 41.7);
        a *= 0.55;
    }
    return v;
}

// Planckian radiator -> linear RGB. Tanner Helland polynomial fit
// (1000 K .. 40000 K), accurate to a few percent, includes the cold-end clamp.
vec3 blackbody(float kelvin) {
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float r, g, b;
    if (t <= 66.0) r = 1.0;
    else           r = 1.292936186062745 * pow(t - 60.0, -0.1332047592);
    if (t <= 66.0) g = 0.3900815787690196 * log(t) - 0.6318414437886270;
    else           g = 1.129890860895294 * pow(t - 60.0, -0.0755148492);
    if (t >= 66.0)      b = 1.0;
    else if (t <= 19.0) b = 0.0;
    else                b = 0.5432067891101961 * log(t - 10.0) - 1.19625408914;
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.xy + p3.yz) * p3.zx);
}

// ------------------------------------------------------------- star field
// Equirectangular mapping on the celestial sphere: correct uniform spherical
// coverage everywhere (no 3-D grid artefacts), inverse-power luminosity
// function, blackbody colours, two magnitude layers.
vec3 starLayer(vec3 dir, vec2 grid, float threshold, float gain) {
    vec2 uv  = vec2(atan(dir.z, dir.x) / (2.0 * PI) + 0.5,
                    asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);
    vec2 p   = uv * grid;
    vec2 cid = floor(p);
    vec2 f   = fract(p);
    vec3 col = vec3(0.0);
    for (int i = -1; i <= 1; ++i)
    for (int j = -1; j <= 1; ++j) {
        vec2 off  = vec2(float(i), float(j));
        vec2 c    = cid + off;
        float h   = hash12(c);
        if (h < threshold) continue;
        float m        = (h - threshold) / (1.0 - threshold);
        vec2  centre   = hash22(c + vec2(17.3, 41.7));
        float d        = length(f - (off + centre));
        float tempK    = mix(2400.0, 24000.0, pow(hash12(c + vec2(53.9, 91.1)), 1.7));
        float radius   = mix(0.055, 0.20, hash12(c + vec2(97.3, 13.9)));
        float falloff  = pow(max(0.0, 1.0 - d / radius), 3.0);
        float mag      = 0.35 + 2.0 * pow(m, 3.0) + 30.0 * pow(m, 15.0);
        col += blackbody(tempK) * falloff * mag * gain;
    }
    return col;
}

vec3 background(vec3 dir) {
    if (uStarsOn == 0) return vec3(0.0);
    vec3 col = vec3(0.0012, 0.0017, 0.0035);         // near-black floor, faint blue
    // Galactic band: great circle tilted out of the grid plane
    vec3  galNormal = normalize(vec3(0.28, 0.92, 0.24));
    float band      = exp(-pow(abs(dot(dir, galNormal)) * 2.7, 2.0));
    float neb       = fbm3(dir * 3.2 + vec3(13.7, 71.3, 29.1));
    float nebFine   = fbm3(dir * 9.5 + vec3(91.3, 7.7, 47.1));
    vec3  nebCol    = mix(vec3(0.028, 0.043, 0.082), vec3(0.070, 0.043, 0.078), nebFine);
    col += band * nebCol * (0.22 + 1.15 * neb);
    col += pow(neb, 3.0) * band * vec3(0.050, 0.042, 0.028);   // faint star-forming glow
    // Star layers: dense dim population + sparse bright stars
    col += starLayer(dir, vec2(620.0, 310.0), 0.982, 0.75);
    col += starLayer(dir, vec2(230.0, 115.0), 0.9915, 1.9);
    return col;
}

// ------------------------------------------------------------ disk model
// Novikov-Thorne (thin, Keplerian) disk, truncated at the ISCO.
// T(r) = T_* * r^{-3/4} (1 - sqrt(r_in/r))^{1/4}, normalised so T(r*) = peak,
// r* = (49/36) r_in.
vec3 diskColor(vec3 hitPos, vec3 photonDirAtHit) {
    float rr   = length(hitPos.xz);
    float phiA = atan(hitPos.z, hitPos.x);
    float rin  = uDiskInner;

    float Tfac   = pow(rr, -0.75) * pow(max(1e-5, 1.0 - sqrt(rin / rr)), 0.25);
    float rstar  = (49.0 / 36.0) * rin;
    float Tstar  = pow(rstar, -0.75) * pow(1.0 - sqrt(rin / rstar), 0.25);
    float tempK  = uDiskPeakTemp * (Tfac / Tstar);

    // Keplerian motion, prograde (angular momentum along +y)
    float Omega   = sqrt(1.0 / (rr * rr * rr));          // geometric units
    vec3  velDir  = vec3(-sin(phiA), 0.0, cos(phiA));    // d(position)/dphi, normalised
    // Local speed measured by a static observer: v = sqrt(M/(r-2M))
    float beta    = sqrt(1.0 / (rr - 2.0));
    float gamma   = inversesqrt(max(1e-5, 1.0 - beta * beta));
    // Frequency shift: gravitational redshift x Doppler
    float grav    = sqrt(clamp(1.0 - 2.0 / rr, 0.0, 1.0));
    vec3  dirToObs = -photonDirAtHit;                   // photon leaving the disk toward the camera
    float cosTheta = dot(dirToObs, velDir);
    float doppler  = 1.0 / (gamma * (1.0 - beta * cosTheta));
    float g        = grav * doppler;

    // Planckian colour of the local rest-frame emission.
    // Honest scaling: bolometric surface flux ~ T^4, specific intensity ~ T^3;
    // observed specific intensity I_nu ~ g^3 I_nu,0 — we use the bolometric-ish
    // T^4 x g^3 law, which is what makes the outer edge visually drop out.
    vec3  bb        = blackbody(tempK);
    float fluxRatio = pow(max(tempK, 1.0) / uDiskPeakTemp, 4.0);  // T^4 scaling
    float boost     = pow(max(g, 1e-3), 3.0);                     // I_nu ~ g^3

    vec3 emit = bb * fluxRatio * boost * uDiskIntensity * 1.6;

    // Corotating MHD-ish turbulence: log spiral arms + fBm, sheared by Omega(r)
    vec3 diskP   = vec3(cos(phiA), sin(phiA), 0.0);
    float turb   = fbm3(diskP * rr * 0.16 + vec3(0.0, 0.0, rr * 0.14 - uTime * Omega * 5.0)
                        + vec3(3.1, 7.7, 11.3));
    float spiral = 0.5 + 0.5 * sin(2.0 * phiA - 4.0 * log(rr) - uTime * Omega * 11.0);
    // Absorbing dust lanes (multiplicative darkening, warm tint)
    float dust   = smoothstep(0.60, 0.98, fbm3(diskP * rr * 0.38 + vec3(0.0, 0.0, 11.7 * log(rr) - uTime * Omega * 3.0)));
    float structure = mix(1.0, mix(0.55, 1.45, turb) * mix(0.88, 1.18, spiral), uDiskSwirl);
    structure *= mix(1.0, 1.0 - 0.62 * dust, uDiskSwirl);
    emit *= structure;

    // Warm glow of the plunging region just outside the ISCO (kept modest)
    emit *= 1.0 + 0.45 * exp(-pow(max(0.0, rr - rin) / 0.7, 2.0));

    // Novikov-Thorne: T -> 0 at r = ISCO, so the inner edge fades in smoothly
    // (this is also the physical antialiasing of the truncation)
    float edgeMask = smoothstep(rin, rin + 0.85, rr) * (1.0 - smoothstep(uDiskOuter - 1.6, uDiskOuter, rr));
    emit *= edgeMask;

    if (uDebug == 1)      gDebug = vec3(clamp(tempK / uDiskPeakTemp, 0.0, 1.0));
    else if (uDebug == 2) gDebug = vec3(clamp((rr - uDiskInner) / (uDiskOuter - uDiskInner), 0.0, 1.0));
    else if (uDebug == 3) gDebug = vec3(clamp(g * 0.6, 0.0, 1.0));
    else if (uDebug == 4) gDebug = vec3(clamp(boost * 0.4, 0.0, 1.0));
    else if (uDebug == 5) gDebug = vec3(clamp(structure * 0.6, 0.0, 1.0));
    else                  gDebug = vec3(-1.0);

    return emit;
}

vec3 acesFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ------------------------------------------------------------------ main
void main() {
    vec2 uv = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    vec3 rd = normalize(uCamFwd + uTanHalfFov * (uv.x * uAspect * uCamRight + uv.y * uCamUp));
    vec3 C  = uCamPos;

    float glow = 0.0;
    bool  captured = false, diskHit = false, woundUp = false;
    vec3  diskPos = vec3(0.0), emitDir = vec3(0.0), tangent = rd;

    if (uLensing == 1) {
        // ---- orbital plane of the photon (contains BH centre, camera, ray)
        vec3 nVec = cross(C, rd);
        float nLen = length(nVec);
        bool headOn = nLen < 1e-5;
        vec3 n = headOn ? ((abs(C.y) < 0.9 * length(C)) ? normalize(cross(C, vec3(0.0, 1.0, 0.0)))
                                                        : normalize(cross(C, vec3(1.0, 0.0, 0.0))))
                        : nVec / nLen;
        vec3 e1 = normalize(C);
        vec3 e2 = cross(n, e1);                 // unit, in-plane, right-handed with n
        float D1 = dot(rd, e1);
        float D2 = dot(rd, e2);                 // == |component perp to e1| >= 0

        float u  = 1.0 / length(C);
        float up = (abs(D2) > 1e-5 && !headOn) ? clamp(-u * D1 / D2, -80.0, 80.0)
                                               : ((D1 < 0.0) ? 80.0 : -80.0);
        float phi = 0.0;
        vec3  pPrev = C;

        for (int i = 0; i < MAX_STEPS; ++i) {
            float r = 1.0 / u;
            // Adaptive dphi: coarse far away, very fine near the photon sphere
            float nearPhoton = clamp(1.0 - abs(r - R_PHOTON) / 9.0, 0.0, 1.0);
            float dphi = mix(0.30, 0.011, nearPhoton * nearPhoton);

            // Classical RK4 on  (u, u'):  u' = v,  v' = 3 u^2 - u
            float k1u = up;
            float k1v = 3.0 * u * u - u;
            float u1  = u + 0.5 * dphi * k1u;
            float v1  = up + 0.5 * dphi * k1v;
            float k2u = v1;
            float k2v = 3.0 * u1 * u1 - u1;
            float u2  = u + 0.5 * dphi * k2u;
            float v2  = up + 0.5 * dphi * k2v;
            float k3u = v2;
            float k3v = 3.0 * u2 * u2 - u2;
            float u3  = u + dphi * k3u;
            float v3  = up + dphi * k3v;
            float k4u = v3;
            float k4v = 3.0 * u3 * u3 - u3;
            u   += dphi * (k1u + 2.0 * k2u + 2.0 * k3u + k4u) / 6.0;
            up  += dphi * (k1v + 2.0 * k2v + 2.0 * k3v + k4v) / 6.0;
            phi += dphi;

            vec3 pNew = (cos(phi) * e1 + sin(phi) * e2) / u;

            // Equatorial accretion disk: y = 0 plane intersection (first hit wins; disk is opaque)
            if (uDiskOn == 1 && pPrev.y * pNew.y < 0.0) {
                float t  = pPrev.y / (pPrev.y - pNew.y);
                vec3  xp = mix(pPrev, pNew, t);
                float rr = length(xp.xz);
                if (rr >= uDiskInner && rr <= uDiskOuter) {
                    diskHit = true;
                    diskPos = xp;
                    emitDir = normalize(xp - pPrev);
                    pPrev   = pNew;
                    break;
                }
            }

            // Integrated "photon hovering" glow near the photon sphere.
            // Accumulates in radians swept; coefficient is deliberately small and
            // the total is clamped — this must produce a thin ring, not a fat halo.
            float rg = 1.0 / u;
            glow += exp(-pow((rg - R_PHOTON) / 0.50, 2.0)) * dphi * 0.055 * (uHaloStrength / 0.9);
            glow = min(glow, 2.2);

            pPrev = pNew;
            float rNow = 1.0 / u;
            if (u >= 0.4999) { captured = true; break; }     // crossed the horizon
            if (rNow > R_ESCAPE && up < 0.0) { break; }      // escaped to infinity
            if (phi > 4.0 * PI) { captured = true; woundUp = true; break; }  // wound up: cannot escape
        }

        // Photon propagation direction at termination (large-r asymptote)
        vec3 eRad = cos(phi) * e1 + sin(phi) * e2;
        vec3 ePhi = -sin(phi) * e1 + cos(phi) * e2;
        tangent = normalize((-up / (u * u)) * eRad + (1.0 / u) * ePhi);
    } else {
        // ---- straight lines (Newtonian / no-lensing comparison mode)
        if (uDiskOn == 1 && abs(rd.y) > 1e-6) {
            float t = -C.y / rd.y;
            if (t > 0.0) {
                vec3 xp = C + rd * t;
                float rr = length(xp.xz);
                if (rr >= uDiskInner && rr <= uDiskOuter) {
                    diskHit = true;
                    diskPos = xp;
                    emitDir = rd;
                }
            }
        }
        tangent = rd;
    }

    vec3 col;
    if (diskHit) {
        vec3 dcol = diskColor(diskPos, emitDir);   // also sets gDebug
        if (uDebug > 0) {
            fragColor = vec4(gDebug, 1.0);          // raw false-colour, no tonemap
            return;
        }
        col  = dcol;
        col += vec3(1.0, 0.82, 0.55) * glow * 0.13;   // glow accrued in front of the disk
    } else if (captured) {
        // Wound-up rays: photons trapped near the photon sphere that neither
        // escaped nor terminated on the disk within the winding budget.
        // Paint a faint ember signature (they live in the photon ring);
        // genuine horizon crossings stay pure black.
        col = woundUp ? (vec3(0.008, 0.003, 0.0006) + vec3(1.0, 0.66, 0.30) * min(glow, 1.0) * 0.05)
                      : vec3(0.0);
    } else {
        col  = background(tangent);
        col += vec3(1.0, 0.85, 0.60) * glow * 0.22;   // photon-ring halo
        if (uDebug > 0) col = vec3(0.02);             // isolate disk for debugging
    }

    col = acesFilm(col * uExposure);
    col = pow(col, vec3(1.0 / 2.2));                   // gamma
    col = clamp((col - 0.028) * 1.10, 0.0, 1.0);       // crush the toe: true blacks
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, 1.22);                  // gentle saturation recovery
    vec2 vg  = uv * vec2(uAspect > 1.0 ? 1.15 : 1.0, 1.0);
    col *= 1.0 - 0.16 * dot(vg, vg) * 0.55;            // vignette
    fragColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------- grid (Flamm)
static const char* kGridVert = R"GLSL(
#version 410 core
layout(location = 0) in vec3 inPos;    // world-space point on the embedding surface
layout(location = 1) in float inDepth; // 0 at the rim .. 1 at the throat
uniform mat4 uViewProj;
out float vDepth;
void main() {
    vDepth = inDepth;
    gl_Position = uViewProj * vec4(inPos, 1.0);
}
)GLSL";

static const char* kGridFrag = R"GLSL(
#version 410 core
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
)GLSL";

// -------------------------------------------------- horizon & marker rings
static const char* kSolidVert = R"GLSL(
#version 410 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    vNormal = inNormal;
    gl_Position = uViewProj * vec4(inPos, 1.0);
}
)GLSL";

static const char* kHorizonFrag = R"GLSL(
#version 410 core
uniform vec3 uCamPos;
in vec3 vNormal;
out vec4 fragColor;
void main() {
    vec3 N = normalize(vNormal);
    // Pure black occluder with a whisper of warm Fresnel at the limb;
    // the horizon itself emits nothing — we cheat only to seat it visually.
    vec3 viewDir = normalize(uCamPos);   // sphere centred on origin
    float fres = pow(1.0 - abs(dot(viewDir, N)), 8.0);
    vec3 col = vec3(0.004, 0.003, 0.002) + vec3(0.20, 0.085, 0.030) * fres * 0.14;
    fragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kRingFrag = R"GLSL(
#version 410 core
uniform vec3 uRingColor;
uniform float uRingAlpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(uRingColor * uRingAlpha, 1.0);
}
)GLSL";

static const char* kRingVert = R"GLSL(
#version 410 core
layout(location = 0) in vec3 inPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(inPos, 1.0);
}
)GLSL";

// ------------------------------------------------------------------- HUD
static const char* kHudVert = R"GLSL(
#version 410 core
layout(location = 0) in vec2 inPx;    // pixel coordinates, origin top-left
layout(location = 1) in vec4 inColor; // premultiplied-ish rgba 0..1
uniform vec2 uViewportPx;
out vec4 vColor;
void main() {
    vColor = inColor;
    vec2 ndc = vec2(inPx.x / uViewportPx.x * 2.0 - 1.0, 1.0 - inPx.y / uViewportPx.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

static const char* kHudFrag = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)GLSL";

} // namespace shaders
