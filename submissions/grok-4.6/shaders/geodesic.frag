#version 410 core
// Schwarzschild null-geodesic ray tracer.
// Geometric units G = c = 1, mass M, horizon rs = 2M, photon sphere 3M,
// ISCO 6M.  Light is integrated backward from a static observer with the
// Hamiltonian
//   H = 1/2 [ -E^2/α + α p_r^2 + p_θ^2/r^2 + Lz^2/(r^2 sin^2 θ) ]
//   α = 1 - 2M/r
// using Heun (RK2).  The accretion disk is a Novikov–Thorne thin disk:
// optically thick, Keplerian, T^4 ∝ r^{-3} (1 - sqrt(risco/r)), with
// gravitational + Doppler transfer g^3 on specific intensity.  A thin
// corona near the photon sphere is accumulated as optically thin emission.

in vec2 vUv;
out vec4 fragColor;

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uTanHalfFov;
uniform float uTime;
uniform float uMass;
uniform int   uEnableDisk;
uniform int   uEnableHalo;
uniform int   uEnableStars;
uniform int   uMaxSteps;
uniform float uDiskOuter;

const float PI = 3.141592653589793;
const int   STEP_CAP = 240;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.07 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

// Planck function sampled at R,G,B effective wavelengths (nm), T in Kelvin.
vec3 blackbody(float T) {
    T = clamp(T, 700.0, 45000.0);
    const float c2 = 1.438777e7;
    vec3 lam = vec3(612.0, 549.0, 464.0);
    vec3 x = min(c2 / (lam * T), 60.0);
    vec3 B = 1.0 / (pow(lam, vec3(5.0)) * (exp(x) - 1.0));
    float n = max(max(B.r, B.g), B.b);
    return B / max(n, 1e-20);
}

vec3 starfield(vec3 dir) {
    vec3 n = normalize(dir);
    vec3 col = vec3(0.0035, 0.0042, 0.0080);
    // Sparse point stars in spherical cells — tight Gaussians, no square blobs.
    for (int oct = 0; oct < 3; ++oct) {
        float dens = 28.0 * pow(2.0, float(oct));
        float th = acos(clamp(n.y, -1.0, 1.0));
        float ph = atan(n.z, n.x);
        vec2 uv = vec2(ph * dens * 0.5, th * dens);
        vec2 id = floor(uv);
        vec2 f = fract(uv) - 0.5;
        float rnd = hash12(id + vec2(float(oct) * 17.2, 4.8));
        if (rnd > 0.965) {
            vec2 jit = vec2(hash12(id + 1.7), hash12(id + 8.3)) - 0.5;
            float d = length(f - jit * 0.35);
            float mag = pow(hash12(id + 5.5), 9.0);
            vec3 tint = mix(vec3(0.68, 0.80, 1.00), vec3(1.00, 0.86, 0.68),
                            hash12(id + 12.0));
            col += tint * mag * exp(-d * d * 1400.0) * 2.4;
        }
    }
    col += exp(-pow(n.y * 3.1, 2.0)) * 0.028 * vec3(0.20, 0.22, 0.36);
    return col;
}

// Novikov–Thorne flux / temperature in the fluid frame.
void diskEmission(float r, float phi, float M, float E, float Lz, float alphaCam,
                  out vec3 rgb, out float alpha)
{
    float risco = 6.0 * M;
    float rout  = uDiskOuter;
    alpha = 0.0;
    rgb = vec3(0.0);
    if (r < risco || r > rout) {
        return;
    }

    // F(r) ∝ r^{-3} (1 - sqrt(risco/r)), zero at ISCO, peak just outside.
    // Peak of that function is near r ≈ 9.5 M; divide by that scale so fluxN ~ 1.
    float x = sqrt(risco / max(r, risco));
    float flux = max(1.0 - x, 0.0) / pow(r / M, 3.0);
    float fluxN = flux / 0.000239;

    // Circular-orbit 4-velocity: Ω = sqrt(M/r^3), u^t = 1/sqrt(1-3M/r).
    float Omega = sqrt(M / (r * r * r));
    float ut = 1.0 / sqrt(max(1.0 - 3.0 * M / r, 1e-4));

    // Frequency shift g = (k·u)_obs / (k·u)_emit.
    // Static camera: (k·u)_obs = E / sqrt(α_cam)
    // Disk:          (k·u)_emit = u^t (E - Ω Lz)
    float kUemit = ut * (E - Omega * Lz);
    float kUobs  = E / max(sqrt(alphaCam), 1e-4);
    float g = clamp(abs(kUobs / max(kUemit, 1e-6)), 0.18, 2.35);

    float Teff = mix(1650.0, 7800.0, pow(clamp(fluxN, 0.0, 1.3), 0.50));
    float Tobs = Teff * g;

    // Keplerian shear + weak logarithmic spiral for the turbulent texture.
    float psi = phi - 0.85 * log(r / risco) - uTime * Omega;
    float turb = fbm(vec2(psi * 2.8, log(r / M) * 3.4));
    turb = mix(0.70, 1.22, turb);

    float flarePhi = uTime * sqrt(M / pow(9.0 * M, 3.0));
    float dphi = atan(sin(phi - flarePhi), cos(phi - flarePhi));
    float flare = exp(-pow(dphi * 2.2, 2.0)) * exp(-pow((r - 9.0 * M) / (3.5 * M), 2.0));
    turb += 0.40 * flare;

    vec3 bb = blackbody(Tobs);
    bb = mix(bb, vec3(1.0, 0.42, 0.10), 0.42);
    float rim = exp(-pow((r - 7.0 * M) / (1.4 * M), 2.0));
    float I = (0.28 * pow(max(fluxN, 0.0), 1.05) + 1.55 * rim) * pow(g, 3.0) * turb;
    float taper = 1.0 - smoothstep(rout * 0.58, rout, r);
    float inner = smoothstep(risco, risco + 0.18 * M, r);
    I *= taper * inner;

    rgb = min(bb * I, vec3(8.0));
    alpha = 1.0;
}

void main() {
    float M = max(uMass, 0.05);
    float rs = 2.0 * M;

    vec2 ndc = vUv * 2.0 - 1.0;
    ndc.x *= uResolution.x / max(uResolution.y, 1.0);
    vec3 rayDir = normalize(uCamForward + ndc.x * uTanHalfFov * uCamRight
                                        + ndc.y * uTanHalfFov * uCamUp);

    vec3 pos = uCamPos;
    float r = length(pos);
    if (r < rs * 1.05) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float th = acos(clamp(pos.y / r, -1.0, 1.0));
    float ph = atan(pos.z, pos.x);

    float sth = sin(th);
    float cth = cos(th);
    float sph = sin(ph);
    float cph = cos(ph);

    // Orthonormal spherical triad (y-up).
    vec3 er = pos / r;
    vec3 et = vec3(cth * cph, -sth, cth * sph);
    vec3 ep = vec3(-sph, 0.0, cph);

    float nr = dot(rayDir, er);
    float nt = dot(rayDir, et);
    float np = dot(rayDir, ep);

    float alphaCam = max(1.0 - rs / r, 1e-4);
    float sA = sqrt(alphaCam);
    float Elocal = 1.0;
    float E = Elocal * sA;

    // Spherical symmetry: every null geodesic lives in a plane through
    // the origin. Integrate in that plane (no polar-axis singularity)
    // and rotate back to world coordinates to test the disk (y = 0).
    vec3 Lvec = Elocal * r * (nt * ep - np * et);
    float L = length(Lvec);
    float Lz = Lvec.y;

    vec3 eR = er;
    vec3 eP = et;
    if (L > 1e-6) {
        eP = normalize(cross(Lvec, eR));
        vec3 tang = nt * et + np * ep;
        if (dot(eP, tang) < 0.0) eP = -eP;
    }

    float rr = r;
    float pr = Elocal * nr / sA;
    float phiOrb = 0.0;

    vec3 color = vec3(0.0);
    float trans = 1.0;
    bool hitDisk = false;
    bool captured = false;

    int limit = min(uMaxSteps, STEP_CAP);
    float yPrev = pos.y;

    for (int i = 0; i < STEP_CAP; ++i) {
        if (i >= limit) {
            captured = (rr < 8.0 * M);
            break;
        }
        if (rr < rs * 1.015 || !(rr == rr)) {
            captured = true;
            break;
        }
        if (rr > 48.0 * M && pr > 0.0) {
            break;
        }

        float h = clamp(0.13 * rr / (1.0 + 6.0 * M / max(rr, 0.3 * M)), 0.014, 0.58);
        float prox = abs(rr - 3.0 * M);
        h *= mix(0.22, 1.0, smoothstep(0.0, 2.6 * M, prox));
        if (rr < 2.7 * M) h *= 0.45;

        float r0 = rr;
        float pr0 = pr;
        float phi0 = phiOrb;

        // RK2 of the equatorial Hamiltonian in the orbital plane.
        float alpha = max(1.0 - rs / max(rr, rs * 1.001), 1e-5);
        float dr1 = alpha * pr;
        float dphi1 = L / (rr * rr);
        float dpr1 = -(M / (rr * rr)) * (E * E / (alpha * alpha) + pr * pr)
                   + (L * L) / (rr * rr * rr);

        float rA = max(rr + h * dr1, rs * 1.001);
        float prA = pr + h * dpr1;
        float alphaA = max(1.0 - rs / rA, 1e-5);
        float dr2 = alphaA * prA;
        float dphi2 = L / (rA * rA);
        float dpr2 = -(M / (rA * rA)) * (E * E / (alphaA * alphaA) + prA * prA)
                   + (L * L) / (rA * rA * rA);

        rr     = max(rr + 0.5 * h * (dr1 + dr2), rs * 1.001);
        pr     = pr + 0.5 * h * (dpr1 + dpr2);
        phiOrb = phiOrb + 0.5 * h * (dphi1 + dphi2);

        vec3 w = rr * (cos(phiOrb) * eR + sin(phiOrb) * eP);
        float y = w.y;

        if (uEnableHalo == 1 && trans > 0.02) {
            float midR = 0.5 * (r0 + rr);
            float midY = 0.5 * (yPrev + y);
            float midTh = acos(clamp(midY / max(midR, 1e-4), -1.0, 1.0));
            float coronaR = exp(-pow((midR - 2.8 * M) / (1.35 * M), 2.0));
            float coronaH = exp(-abs(midTh - 0.5 * PI) * 5.5);
            float dlam = abs(h);
            float gCor = clamp(sqrt(max(1.0 - rs / max(midR, rs * 1.05), 0.0)), 0.05, 1.0);
            float emit = coronaR * coronaH * dlam * 0.22 * pow(gCor, 3.0);
            if (midR < 3.8 * M && midR > 2.4 * M) {
                emit += 0.010 * dlam * gCor * gCor * coronaR;
            }
            color += trans * emit * vec3(1.00, 0.58, 0.24);
        }

        if (uEnableDisk == 1 && !hitDisk && yPrev * y < 0.0) {
            float t = yPrev / (yPrev - y + 1e-8);
            float rc = mix(r0, rr, t);
            vec3 wc = mix(r0 * (cos(phi0) * eR + sin(phi0) * eP), w, t);
            float pc = atan(wc.z, wc.x);
            vec3 rgb;
            float a;
            diskEmission(rc, pc, M, E, Lz, alphaCam, rgb, a);
            if (a > 0.5) {
                color += trans * rgb;
                hitDisk = true;
                break;
            }
        }
        yPrev = y;
    }

    if (!hitDisk) {
        if (captured) {
            color += trans * vec3(0.0);
        } else {
            vec3 erOut = cos(phiOrb) * eR + sin(phiOrb) * eP;
            vec3 epOut = -sin(phiOrb) * eR + cos(phiOrb) * eP;
            float al = max(1.0 - rs / max(rr, rs * 1.05), 1e-4);
            vec3 dirOut = normalize(erOut * (pr * sqrt(al)) + epOut * (L / max(rr, 1e-3)));
            vec3 sky = (uEnableStars == 1) ? starfield(dirOut) : vec3(0.008, 0.010, 0.018);
            color += trans * sky;
        }
    }

    fragColor = vec4(color, 1.0);
}
