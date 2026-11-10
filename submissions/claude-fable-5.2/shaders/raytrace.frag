#version 410 core
// =============================================================================
//  raytrace.frag — one null geodesic of the Kerr metric per pixel.
//
//  The geodesic integrator lives in kerr_geodesic.glsl (shared with the CPU
//  verification).  This file supplies the camera, the accretion disk and the
//  sky.
// =============================================================================
#include "kerr_geodesic.glsl"

layout(location = 0) out vec4  oColor;     // linear HDR radiance
layout(location = 1) out float oDepth;     // Euclidean distance to first opaque hit

uniform vec2  uResolution;
uniform vec2  uJitter;          // sub-pixel offset for progressive supersampling
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uPixelAngle;      // angular size of a pixel (radians)

uniform vec3  uCamPos;          // Kerr-Schild Cartesian position of the observer
uniform vec4  uTetT, uTetR, uTetU, uTetF;   // observer tetrad, components (x,y,z,t)

uniform float uSpin;            // physical spin a/M (hole spins about +z)
uniform float uRh;              // outer horizon
uniform float uRfar;
uniform float uEps;
uniform int   uMaxSteps;

uniform float uDiskIn;          // ISCO
uniform float uDiskOut;
uniform float uLutRmax;         // radius covered by the temperature table
uniform float uTpeak;           // maximum disk temperature [K]
uniform vec2  uLogT;            // ln(Tmin), ln(Tmax) of the blackbody table
uniform float uLutN;            // blackbody table size
uniform float uLogLumRef;       // ln(luminance) of a blackbody at uTpeak
uniform float uDiskContrast;
uniform float uTime;            // observer coordinate time [M]
uniform float uSkyGain;
uniform int   uFlags;           // 1 doppler+redshift | 2 turbulence | 4 limb darkening
                                // 8 disk | 16 sky | 32 redshift map | 64 lensed plane grid

uniform samplerCube uSky;
uniform sampler2D   uBlackbody;
uniform sampler2D   uDiskLUT;

const float PI = 3.14159265358979;

bool flag(int bit) { return (uFlags & bit) != 0; }

// ---------------------------------------------------------------------------
// Blackbody radiance in linear sRGB, relative to a blackbody at uTpeak.
// The table stores chromaticity and ln(luminance) on a logarithmic T axis.
// ---------------------------------------------------------------------------
vec3 blackbody(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    vec4 s = texture(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5));
    return s.rgb * exp(s.a - uLogLumRef);
}

vec3 bbChroma(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    return texture(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5)).rgb;
}

#include "stars.glsl"

// ---------------------------------------------------------------------------
// Bright stars as true point sources.
// A star is unresolved, so lensing cannot change its surface brightness the
// way it does for the disk - it changes its *flux*, by the magnification
//     mu = (solid angle of a pixel) / (solid angle of sky that pixel sees).
// gx, gy = d(dir)/d(pixel) is the lens map's Jacobian; inverting it turns the
// angular offset to the star into an offset in pixels, where the star is
// drawn with a fixed, small point-spread function.  Stars brighten into arcs
// near the Einstein ring and fade where the sky is squeezed towards the
// photon ring.
// ---------------------------------------------------------------------------
vec3 pointStars(vec3 d, vec3 gx, vec3 gy) {
    float A = dot(gx, gx), B = dot(gx, gy), C = dot(gy, gy);
    float pix2 = uPixelAngle * uPixelAngle;
    float det = max(A * C - B * B, pix2 * pix2 / 1600.0);     // caps mu at 40
    float mu = pix2 / sqrt(det);
    float foot = sqrt(max(A, C));                             // sky angle per pixel

    float lat, lon;
    float density = clamp(0.30 + 0.70 * min(galacticDensity(d, lat, lon), 1.0), 0.0, 1.0);

    const float s2 = 0.72 * 0.72;                             // PSF variance [pixel^2]
    vec3 col = vec3(0.0);
    for (int L = 0; L < 2; ++L) {
        vec3 sd;
        float flux, T;
        if (!starInCell(d, L, density, sd, flux, T)) continue;
        float cellAngle = 2.0 / (STAR_LAYER0_CELLS * exp2(float(L)));
        // drop out where the pixel sees more sky than a catalogue cell, and
        // on the critical curve itself, where the linearised lens map fails
        float fade = (1.0 - smoothstep(0.06 * cellAngle, 0.2 * cellAngle, foot))
                   * (1.0 - smoothstep(12.0, 36.0, mu));
        if (fade <= 0.0) continue;
        vec3 dl = sd - d;
        float u = dot(dl, gx), v = dot(dl, gy);
        float a = (C * u - B * v) / det, b = (A * v - B * u) / det;
        float q = a * a + b * b;
        col += bbChroma(T) * (flux * fade * mu * STAR_SIGMA0 * STAR_SIGMA0 / (s2 * pix2)) * exp(-q / (2.0 * s2));
    }
    return col;
}

// ---------------------------------------------------------------------------
// Value noise, periodic in x (the azimuthal lattice direction).
// ---------------------------------------------------------------------------
float hash3(int x, int y, int seed) {
    return float(pcg(uint(x) + pcg(uint(y) + pcg(uint(seed))))) * (1.0 / 4294967296.0);
}
int wrap(int x, int n) { return ((x % n) + n) % n; }

float vnoise(vec2 p, int period, int seed) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    int x0 = wrap(int(i.x), period), x1 = wrap(int(i.x) + 1, period);
    int y0 = int(i.y), y1 = y0 + 1;
    return mix(mix(hash3(x0, y0, seed), hash3(x1, y0, seed), u.x),
               mix(hash3(x0, y1, seed), hash3(x1, y1, seed), u.x), u.y);
}
float fbm(vec2 p, int period, int seed) {
    float a = 0.5, s = 0.0, n = 0.0;
    for (int o = 0; o < 5; ++o) {
        s += a * vnoise(p, period, seed + 17 * o);
        n += a;
        p *= 2.0;
        period *= 2;
        a *= 0.5;
    }
    return s / n;
}

// ---------------------------------------------------------------------------
// Turbulent emissivity pattern, advected with the Keplerian flow.
//
// Every annulus carries its pattern around at exactly Omega(r), so
// differential rotation shears blobs into trailing spirals.  Left alone the
// winding would grow without bound, so the pattern is regenerated every
// `turns` local orbits (about the coherence time of magnetorotational
// turbulence).  "Local" means per radial band: bands are half an e-fold wide,
// two interleaved sets of them and two half-cycle-offset generations per band
// are cross-faded, so nothing ever pops or shows a seam.
// Evaluated at the *emission* time, so light-travel-time delays across the
// disk are included.  Returns a zero-mean field of roughly constant variance.
// ---------------------------------------------------------------------------
float diskTurbulence(float r, float phi, float tEmit, float Omega) {
    const float turns = 0.6;
    const int   N = 12;                      // azimuthal lattice period
    const float bandWidth = 0.5;             // in ln r
    float lr = log(r);
    float acc = 0.0, w2 = 0.0;
    for (int s = 0; s < 2; ++s) {
        float bx = lr / bandWidth + 0.5 * float(s);
        float bi = floor(bx);
        float wb = 1.0 - abs(2.0 * (bx - bi) - 1.0);
        float rb = exp((bi + 0.5 - 0.5 * float(s)) * bandWidth);       // band centre
        float Tc = 2.0 * PI * turns * (rb * sqrt(rb) + uSpin);          // regeneration period
        float cyc = tEmit / Tc;
        for (int k = 0; k < 2; ++k) {
            float c  = cyc + 0.5 * float(k);
            float ci = floor(c);
            float fr = c - ci;
            float wt = 1.0 - abs(2.0 * fr - 1.0);
            float ang = phi - Omega * fr * Tc;           // co-rotating azimuth since regeneration
            int seed = int(bi) * 7919 + int(ci) * 104729 + s * 31 + k * 17 + 101;
            float n = fbm(vec2(ang * float(N) / (2.0 * PI), lr * 3.0), N, seed);
            float w = wb * wt;
            acc += w * (n - 0.5);
            w2  += w * w;
        }
    }
    return acc / sqrt(max(w2, 1.0e-6));
}

// Smooth blue-white-red map for the redshift diagnostic view.
vec3 redshiftMap(float g) {
    float x = clamp(log2(g) / 0.75, -1.0, 1.0);
    vec3 red = vec3(1.0, 0.16, 0.05), white = vec3(0.9), blue = vec3(0.08, 0.35, 1.0);
    return x < 0.0 ? mix(white, red, -x) : mix(white, blue, x);
}

// ---------------------------------------------------------------------------
// Appearance of the equatorial plane at a ray crossing (called by traceRay).
// ---------------------------------------------------------------------------
vec4 planeShade(vec3 x, vec3 p, float E, float t, float rc) {
    vec4 result = vec4(0.0);

    if (flag(8) && rc > uDiskIn && rc < uDiskOut) {
        // --- Novikov-Thorne disk: gas on circular geodesics, +phi direction ---
        float Omega = 1.0 / (rc * sqrt(rc) + uSpin);
        // In the time-reversed chart the gas orbits backwards.
        float g  = diskRedshift(x, p, E, -uSpin, rc, -Omega);
        // cosine of the emission angle from the disk normal, in the gas frame
        float mu = clamp(abs(p.z) * g, 0.0, 1.0);

        float u  = sqrt((rc - uDiskIn) / (uLutRmax - uDiskIn));
        float Tn = texture(uDiskLUT, vec2((u * 1023.0 + 0.5) / 1024.0, 0.5)).r;   // (F/Fmax)^(1/4)

        float emis = 1.0;
        if (flag(2)) {
            float phi = atan(x.y, x.x);
            float turb = diskTurbulence(rc, phi, uTime - t, Omega);
            emis = exp(uDiskContrast * turb);
        }
        float T = uTpeak * Tn * pow(emis, 0.25);        // flux ~ T^4

        // I_nu / nu^3 is invariant: the observer sees a blackbody at g*T.
        float gEff = flag(1) ? g : 1.0;
        vec3 rad = blackbody(max(T * gEff, 1.0));

        // Electron-scattering atmosphere (Chandrasekhar): I(mu) ~ 1 + 2.06 mu
        if (flag(4)) rad *= (3.0 / 7.0) * (1.0 + 2.0 * mu);

        if (flag(32)) rad = redshiftMap(g) * (0.25 + 0.75 * Tn) * 0.6;

        // Opaque (optically thick) body; the surface density is taken to
        // taper off over the outer fifth so the rim is not a hard edge.
        float alpha = smoothstep(uDiskIn, uDiskIn + 0.12, rc)
                    * (1.0 - smoothstep(uDiskOut * 0.78, uDiskOut, rc));
        result = vec4(rad * alpha, alpha);
    }

    if (flag(64)) {
        // --- coordinate grid painted on the equatorial plane, seen through
        //     the same curved light paths (drawn where the disk is not) ---
        float phi = atan(x.y, x.x);
        // footprint of a pixel on the plane ~ distance * pixel angle / cos(incidence)
        float foot = uPixelAngle * t * length(p) / max(abs(p.z), 0.02 * length(p));
        float wr = max(0.035, 1.2 * foot);
        float dr = abs(fract(rc * 0.5 + 0.5) - 0.5) * 2.0;               // rings every 2 M
        float dp = abs(fract(phi * 12.0 / PI + 0.5) - 0.5) * (PI / 12.0) * rc;   // spokes every 15 deg
        float line = max(1.0 - smoothstep(0.5 * wr, wr, dr), 1.0 - smoothstep(0.5 * wr, wr, dp));
        float fade = (1.0 - smoothstep(0.6 * uRfar, 0.95 * uRfar, rc)) * 0.035 / max(0.035, foot * 1.2);
        float a = 0.55 * line * clamp(fade, 0.0, 1.0) * (1.0 - result.a);
        result.rgb += vec3(0.10, 0.45, 1.0) * a * 0.9;
        result.a   += a * 0.5;
    }
    return result;
}

void main() {
    vec2 px  = gl_FragCoord.xy + uJitter;
    vec2 ndc = px / uResolution * 2.0 - 1.0;
    vec2 screen = vec2(ndc.x * uAspect, ndc.y) * uTanHalfFov;

    float aChart = -uSpin;                    // see the header of kerr_geodesic.glsl
    vec4 pe = ksCameraRay(uCamPos, uTetT, uTetR, uTetU, uTetF, screen, aChart);

    TraceParams P;
    P.a         = aChart;
    P.rHorizon  = uRh;
    P.rFar      = uRfar;
    P.eps       = uEps;
    P.maxSteps  = uMaxSteps;
    P.rPlaneMin = uRh;
    P.rPlaneMax = uRfar;

    TraceResult R = traceRay(uCamPos, pe.xyz, pe.w, P);

    // Screen-space footprint of the sky direction (taken outside any branch so
    // the derivatives are well defined): the Jacobian of the lens map.
    vec3 dir = R.dir;
    vec3 jx = dFdx(dir), jy = dFdy(dir);
    vec3 gx = jx, gy = jy;                    // clamped copy picks the cube-map mip level
    float gm = max(length(gx), length(gy));
    float lim = 0.08;
    if (gm > lim) { gx *= lim / gm; gy *= lim / gm; }

    vec3 col = R.color;
    if (R.status == 0 && flag(16)) {
        vec3 sky = textureGrad(uSky, dir, gx, gy).rgb + pointStars(dir, jx, jy);
        // A static observer deep in the potential sees the sky blueshifted by
        // g = 1/E (E = photon energy at infinity for unit local energy);
        // bolometric intensity scales as g^4.
        float g = 1.0 / pe.w;
        col += R.trans * sky * (uSkyGain * g * g * g * g);
    }

    float depth = R.depth;
    if (R.status == 1) depth = min(depth, length(uCamPos));   // the hole hides what lies behind it

    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    oColor = vec4(col, 1.0);
    oDepth = depth;
}
