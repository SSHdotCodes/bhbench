#version 300 es
precision highp float;
precision highp int;
vec4 nativeLookup(sampler2D table,vec2 uv){
 int n=textureSize(table,0).x;float x=uv.x*float(n)-.5;int i=int(floor(x));
 return mix(texelFetch(table,ivec2(clamp(i,0,n-1),0),0),texelFetch(table,ivec2(clamp(i+1,0,n-1),0),0),fract(x));
}
//
// Realtime gravitational lensing around a Schwarzschild black hole.
//
// Every photon obeys the exact orbit equation (u = 1/r in the photon's plane)
//        d^2u/dphi^2 + u = 3 M u^2
// integrated here with adaptive RK4.  The impact parameter follows from the
// first integral  1/b^2 = (du/dphi)^2 + u^2 (1 - 2M u),  b = L/E.
//
// The accretion flow is a razor-thin optically thick Novikov-Thorne disk
// (ISCO at 6M) plus an optically thin flared atmosphere ("halo").  Emission is
// thermally shifted and beamed by
//        g = sqrt(1 - rs/r) / (1 - Omega * L_z/E),  Omega = sqrt(M/r^3)
// with bolometric intensity transformed as I_obs = g^4 I_emit.
//
layout(location = 0) out vec4 outColor;

uniform vec2  uRes;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform int   uMaxSteps;
uniform float uStepMax;
uniform float uRFar;
uniform float uDiskInner;
uniform float uDiskOuter;
uniform float uHaloOuter;
uniform float uHaloH0;
uniform float uHaloFlare;
uniform float uHaloEmis;
uniform float uHaloOpacity;
uniform float uF0;
uniform float uFPeak;
uniform float uTempScale;
uniform float uTurb;
uniform float uBgScale;
uniform int   uShowDisk;
uniform int   uShowHalo;
uniform int   uShowStars;
uniform int   uDoppler;
uniform sampler2D uBB;

const float M     = 1.0;
const float RS    = 2.0;
const float PI    = 3.141592653589793;
const float SIGMA = 5.670374e-5;   // Stefan-Boltzmann, cgs
const float LOG_TMIN   = 5.703782; // ln(300 K)
const float LOG_TRANGE = 10.414334; // ln(1e7 / 300)

// ---------------------------------------------------------------------------
// Integrator
// ---------------------------------------------------------------------------
vec2 deriv(vec2 y) { return vec2(y.y, 3.0 * M * y.x * y.x - y.x); }

vec2 rk4(vec2 y, float h) {
    vec2 k1 = deriv(y);
    vec2 k2 = deriv(y + 0.5 * h * k1);
    vec2 k3 = deriv(y + 0.5 * h * k2);
    vec2 k4 = deriv(y + h * k3);
    return y + (h / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

float stepH(float u) { return uStepMax / (1.0 + 6.0 * u + 25.0 * u * u); }

// ---------------------------------------------------------------------------
// Colour: observed blackbody temperature -> linear sRGB (LUT built on the CPU
// by integrating Planck x CIE 1931 x XYZ->sRGB).
// ---------------------------------------------------------------------------
vec3 lutColor(float T) {
    float t = (log(max(T, 1.0)) - LOG_TMIN) / LOG_TRANGE;
    return nativeLookup(uBB, vec2(clamp(t, 0.002, 0.998), 0.5)).rgb;
}

// ---------------------------------------------------------------------------
// Hash / value noise / fbm (for disk structure and the star field)
// ---------------------------------------------------------------------------
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

float vnoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) {
        s += a * vnoise(p);
        p = p * 2.07 + vec3(11.3, 7.7, 3.1);
        a *= 0.5;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Redshift / beaming factor for gas in circular Keplerian orbit
// ---------------------------------------------------------------------------
float gFactor(float r, float bLz) {
    float Om = inversesqrt(r * r * r);                 // sqrt(M/r^3), M = 1
    float denom = 1.0 - Om * bLz;
    if (uDoppler == 0) denom = 1.0;                    // verification switch
    float grav = sqrt(max(1.0 - RS / r, 1e-4));
    return grav / max(denom, 0.05);
}

// ---------------------------------------------------------------------------
// Optically thick thin-disk surface (Novikov-Thorne, a = 0)
// ---------------------------------------------------------------------------
vec3 diskEmission(float r, float g, float phi) {
    float ri = uDiskInner;
    float shape = (1.0 - sqrt(ri / r)) / (r * r * r);
    if (shape < 0.0) shape = 0.0;
    float F = uF0 * shape;                             // erg cm^-2 s^-1
    float g4 = g * g * g * g;
    // pattern advected with the local Keplerian angular velocity (shear)
    float pc = phi - uTime * pow(r, -1.5);
    vec3 q = vec3(r * cos(pc), r * sin(pc), 0.0) * 0.20;
    float turb = 1.0 + uTurb * (fbm(q) - 0.5) * 2.0;
    float T = sqrt(sqrt(max(F, 1e-30) / SIGMA));       // effective temperature, K
    return lutColor(g * T / uTempScale) * (g4 * (F / uFPeak) * turb);
}

// ---------------------------------------------------------------------------
// Optically thin flared atmosphere ("halo"), integrated along the curved ray.
// Bolometric emissivity is beamed as g^4; extinction uses the same density.
// ---------------------------------------------------------------------------
void haloSeg(vec3 pA, vec3 pB, float bLz, inout vec3 col, inout float trans) {
    if (uShowHalo == 0 || trans < 0.02) return;
    vec3 pm = 0.5 * (pA + pB);
    float rm = length(pm);
    if (rm > uHaloOuter || rm < RS * 1.02) return;

    float H = uHaloH0 * rm * pow(rm / RS, uHaloFlare); // scale height, flared
    float vert = exp(-pm.z * pm.z / (2.0 * H * H));
    if (vert < 1e-4) return;
    float taper = smoothstep(0.40, 1.05, rm / uDiskInner);
    float rho = vert * pow(rm / RS, -2.0) * taper;
    if (rho < 1e-7) return;

    float rmx = max(rm, uDiskInner);
    float shape = (1.0 - sqrt(uDiskInner / rmx)) / (rmx * rmx * rmx);
    if (shape < 0.0) shape = 0.0;

    // proper path element of a static observer (radial stretching ~ 1/sqrt(1-rs/r))
    float ds = length(pB - pA) / sqrt(max(1.0 - RS / rm, 0.05));

    float g = gFactor(rm, bLz);
    float g4 = g * g * g * g;
    float F = uF0 * shape;
    float T = sqrt(sqrt(max(F, 1e-30) / SIGMA));
    // optically thin emissivity follows the density (rho0 = density at r = 10M
    // in the mid plane, used to keep the coefficient O(1))
    float rhoN = rho * 25.0;
    col += trans * lutColor(g * T / uTempScale) * (g4 * F / uFPeak) * rhoN * uHaloEmis * ds;
    trans *= exp(-uHaloOpacity * rho * ds);
}

// ---------------------------------------------------------------------------
// Background: Milky Way band + equal-area star field (lensed by the geodesics)
// ---------------------------------------------------------------------------
vec3 starField(vec3 d) {
    vec3 col = vec3(0.0);

    vec3 gn = normalize(vec3(0.28, 0.42, 0.86));
    float lat = dot(d, gn);
    float band = exp(-lat * lat * 60.0);
    float n = fbm(d * 7.0);
    float dust = fbm(d * 17.0 + vec3(9.0, 4.0, 1.5));
    vec3 c1 = vec3(0.55, 0.62, 0.92);
    vec3 c2 = vec3(0.98, 0.74, 0.48);
    vec3 mw = mix(c1, c2, smoothstep(0.45, 0.72, n));
    mw *= band * (0.16 + 1.0 * n * n);
    mw *= 1.0 - 0.75 * smoothstep(0.44, 0.60, dust);
    col += mw * uBgScale * 0.035;

    // equal-area cells on the sphere: (azimuth, cos inclination)
    float s = 0.5 + 0.5 * d.z;
    float phi = atan(d.y, d.x) * 0.1591549431 + 0.5;
    vec2 grid = vec2(314.0, 100.0);
    vec2 vUV = vec2(phi, s) * grid;
    vec2 base = floor(vUV);
    vec2 f = vUV - base;
    vec2 cell = vec2(mod(base.x, grid.x), base.y);
    vec3 rnd = hash33(vec3(cell, 17.0));
    float mag = pow(rnd.z, 7.0);
    vec2 sp = rnd.xy * 0.7 + 0.15;
    float dd = length(f - sp);
    float core = exp(-dd * dd * 70.0);
    float glow = exp(-dd * dd * 7.0) * 0.10;
    vec3 tint = lutColor(2600.0 + 11000.0 * rnd.y * rnd.y);
    col += mag * (core + glow) * 7.0 * tint * uBgScale;

    return col;
}

// ---------------------------------------------------------------------------
void main() {
    vec2 ndc = (gl_FragCoord.xy - 0.5 * uRes) / (0.5 * uRes.y);
    vec3 dir = normalize(uCamFwd + uTanHalfFov * (ndc.x * uCamRight + ndc.y * uCamUp));

    float r0 = length(uCamPos);
    vec3 e1 = uCamPos / r0;
    float vr = dot(dir, e1);
    vec3 dperp = dir - vr * e1;
    float vt = length(dperp);

    vec3 col = vec3(0.0);
    float trans = 1.0;

    // Exactly radial ray: zero angular momentum, the solution is a straight line.
    if (vt < 1e-6) {
        if (vr > 0.0 && uShowStars == 1) col = starField(dir);
        outColor = vec4(col, 1.0);
        return;
    }

    // Orbital plane basis: e1 radial, e2 along the transverse velocity.
    vec3 e2 = dperp / vt;
    vec3 e3 = cross(e1, e2);          // normal, along L = r x p
    float nz = e3.z;
    float u = 1.0 / r0;
    float w = -u * vr / vt;           // du/dphi
    float invB2 = max(w * w + u * u * (1.0 - 2.0 * M * u), 1e-12);
    float b = inversesqrt(invB2);      // impact parameter L/E
    float bLz = b * nz;               // L_z/E

    bool equatorial = (e1.z * e1.z + e2.z * e2.z) < 1e-12;
    bool done = false;
    bool captured = false;

    // Degenerate case: the camera sits inside the disk annulus in its plane.
    if (uShowDisk == 1 && equatorial && r0 >= uDiskInner && r0 <= uDiskOuter) {
        col += trans * diskEmission(r0, gFactor(r0, bLz), 0.0);
        trans = 0.0;
        done = true;
    }

    float phi = 0.0;
    vec3 posPrev = uCamPos;
    float zPrev = equatorial ? 1e-6 : uCamPos.z;
    float uFar = 1.0 / uRFar;

    for (int i = 0; i < uMaxSteps && !done; ++i) {
        if (u >= 0.5) { captured = true; break; }              // crossed the horizon
        if (w < 0.0 && u <= uFar) break;                       // escaped to infinity

        float h = stepH(u);
        if (w < 0.0) {                                          // never step u through zero
            float hl = (u - 0.99 * uFar) / max(-w, 1e-9);
            if (hl < h) h = max(hl, 1e-7);
        }
        if (uShowDisk == 1 && abs(zPrev) < 0.6 && (1.0 / u) < uDiskOuter * 1.4)
            h = min(h, 0.14);                                   // do not jump the disk plane

        vec2 yn = rk4(vec2(u, w), h);
        if (!(yn.x > 0.0)) break;
        float phiN = phi + h;
        float rN = 1.0 / yn.x;
        vec3 posN = rN * (cos(phiN) * e1 + sin(phiN) * e2);
        float zN = posN.z;

        // ---- disk intersection ----
        if (uShowDisk == 1) {
            bool hit = false;
            float rHit = 0.0, phHit = 0.0;
            vec3 posHit = posN;

            if (equatorial) {
                if (rN <= uDiskOuter && rN >= uDiskInner) {
                    hit = true; rHit = rN; phHit = phiN; posHit = posN;
                }
            } else if (zPrev * zN < 0.0) {
                float t = zPrev / (zPrev - zN);
                float rEst = 1.0 / max(mix(u, yn.x, t), 1e-6);
                if (rEst >= uDiskInner && rEst <= uDiskOuter) {
                    // bisect the actual trajectory for a sub-pixel accurate hit
                    float lo = 0.0, hi = h;
                    vec2 yLo = vec2(u, w);
                    for (int k = 0; k < 8; ++k) {
                        float mid = 0.5 * (lo + hi);
                        vec2 ym = rk4(vec2(u, w), mid);
                        float pm = phi + mid;
                        float zm = (1.0 / ym.x) * (cos(pm) * e1.z + sin(pm) * e2.z);
                        if (zm * zPrev > 0.0) { lo = mid; yLo = ym; }
                        else { hi = mid; }
                    }
                    float rh = 1.0 / yLo.x;
                    if (rh >= uDiskInner && rh <= uDiskOuter) {
                        hit = true;
                        rHit = rh;
                        phHit = phi + lo;
                        posHit = rh * (cos(phHit) * e1 + sin(phHit) * e2);
                    }
                }
            }

            if (hit) {
                haloSeg(posPrev, posHit, bLz, col, trans);
                col += trans * diskEmission(rHit, gFactor(rHit, bLz), phHit);
                trans = 0.0;
                done = true;
                break;
            }
        }

        haloSeg(posPrev, posN, bLz, col, trans);

        u = yn.x;
        w = yn.y;
        phi = phiN;
        posPrev = posN;
        zPrev = zN;
    }

    bool finite = (u > 0.0) && (w == w) && (trans == trans);
    if (!captured && !done && finite && trans > 0.004 && uShowStars == 1) {
        float drdphi = -w / (u * u);
        float sp = sin(phi), cp = cos(phi);
        vec3 er = cp * e1 + sp * e2;
        vec3 eph = -sp * e1 + cp * e2;
        vec3 esc = normalize(drdphi * er + (1.0 / u) * eph);
        col += trans * starField(esc);
    }

    outColor = vec4(max(col, vec3(0.0)), 1.0);
}
