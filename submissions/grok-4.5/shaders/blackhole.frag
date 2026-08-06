#version 410 core

// ============================================================================
// Scientifically grounded Schwarzschild black-hole ray tracer
// ----------------------------------------------------------------------------
// Geometric units: G = c = 1. Mass parameter M. Schwarzschild radius rs = 2M.
//
// Null geodesics: any single geodesic of the spherically symmetric Schwarzschild
// spacetime lies in a plane. We integrate the exact planar orbit equation
//
//     d²u / dφ² + u = 3 M u² ,   u = 1/r
//
// with RK4 in the orbital plane of each camera ray (impact parameter b = L/E).
// Horizon capture: r ≤ rs (+ epsilon). Photon sphere: r_ph = 3M (unstable).
//
// Accretion disk: geometrically thin equatorial disk (Novikov–Thorne style)
// from r_isco = 6M to r_out, with T_eff ~ r^{-3/4}-family profile, gravitational
// redshift √(1 − rs/r), and Keplerian Doppler factor for orbital motion.
// Photon-ring / halo: rays that wind near the photon sphere pick up extra
// disk intersections and gravitational blueshift/redshift, forming bright rings.
// ============================================================================

in vec2 vUV;
out vec4 fragColor;

// Camera
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform float uTanHalfFov;
uniform float uAspect;

// Physics
uniform float uM;              // mass
uniform float uTime;           // animation time
uniform int   uMaxSteps;       // integration steps
uniform float uDiskInner;      // usually 6M (ISCO)
uniform float uDiskOuter;
uniform int   uShowDisk;
uniform int   uQuality;        // 0=fast, 1=normal, 2=high

const float PI = 3.14159265358979323846;
const float TWO_PI = 6.28318530717958647692;

// ---- utilities -------------------------------------------------------------

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Simple star field for the celestial sphere (background at infinity).
vec3 starField(vec3 dir) {
    dir = normalize(dir);
    float u = 0.5 + atan(dir.z, dir.x) / TWO_PI;
    float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI;
    vec2 uv = vec2(u, v) * 900.0;

    vec2 cell = floor(uv);
    float n = hash21(cell);
    float star = 0.0;
    if (n > 0.996) {
        vec2 f = fract(uv) - 0.5;
        float d = length(f);
        star = smoothstep(0.06, 0.0, d) * (n - 0.996) * 400.0;
    }
    // Second layer of fainter stars
    vec2 cell2 = floor(uv * 0.37 + 17.0);
    float n2 = hash21(cell2);
    if (n2 > 0.993) {
        vec2 f2 = fract(uv * 0.37 + 17.0) - 0.5;
        star += smoothstep(0.08, 0.0, length(f2)) * 0.6;
    }

    float band = exp(-pow(dir.y * 3.2, 2.0)) * 0.06;
    vec3 bg = vec3(0.01, 0.014, 0.03) + band * vec3(0.12, 0.10, 0.16);
    bg += star * vec3(1.0, 0.96, 0.9);

    float neb = 0.5 + 0.5 * sin(dir.x * 3.0 + dir.z * 2.0);
    bg += neb * 0.02 * vec3(0.35, 0.12, 0.55);
    return bg;
}

// Blackbody-ish palette for accretion temperature (approximate CIE mapping).
vec3 tempToColor(float t) {
    // t normalized ~ 0..1, hotter → bluer/white
    t = clamp(t, 0.0, 1.5);
    vec3 cold = vec3(1.0, 0.15, 0.02);
    vec3 mid  = vec3(1.0, 0.55, 0.12);
    vec3 hot  = vec3(1.0, 0.92, 0.75);
    vec3 blue = vec3(0.75, 0.85, 1.0);
    vec3 c;
    if (t < 0.35) {
        c = mix(cold, mid, t / 0.35);
    } else if (t < 0.75) {
        c = mix(mid, hot, (t - 0.35) / 0.40);
    } else {
        c = mix(hot, blue, clamp((t - 0.75) / 0.75, 0.0, 1.0));
    }
    return c * (0.4 + 2.2 * t);
}

// Disk emissivity proxy ~ r^{-3/4} family with ISCO cutoff (Page–Thorne-like).
float diskProfile(float r, float rIn, float rOut) {
    if (r < rIn || r > rOut) return 0.0;
    float x = rIn / max(r, 1e-4);
    // (1 - sqrt(rIn/r)) softens emission near ISCO; overall falls with radius
    float f = pow(x, 3.0) * max(1.0 - sqrt(x), 0.0);
    // Outer taper
    float taper = 1.0 - smoothstep(rOut * 0.75, rOut, r);
    return f * taper;
}

// Keplerian orbital speed (physical velocity / c) for circular geodesic:
//   Ω = sqrt(M / r³),  v_φ = Ω r / sqrt(1 − 3M/r)   (as measured at infinity factor handled separately)
float keplerSpeed(float r, float M) {
    float rs = 2.0 * M;
    if (r <= 3.0 * M) return 0.0; // inside photon sphere / ISCO region for photons of matter
    float v = sqrt(M / r) / sqrt(max(1.0 - 3.0 * M / r, 1e-4));
    return clamp(v, 0.0, 0.95);
}

// ---- planar null geodesic (exact Schwarzschild orbit equation) -------------
//
// In the orbital plane, with conserved impact parameter b = L/E:
//   (du/dφ)² = 2 M u³ − u² + 1/b²
// or second-order form used here:
//   d²u/dφ² = 3 M u² − u
//
// Critical impact parameter for capture (unstable photon orbit):
//   b_c = 3√3 M ≈ 5.196 M
// Rays with b < b_c that are inbound fall into the horizon.

struct GeoResult {
    vec3 color;
    float alpha;
    float capture; // 1 if swallowed
};

// Integrate geodesic given camera ray; return radiance.
GeoResult traceGeodesic(vec3 origin, vec3 dir, float M) {
    GeoResult res;
    res.color = vec3(0.0);
    res.alpha = 0.0;
    res.capture = 0.0;

    float rs = 2.0 * M;
    float rPh = 3.0 * M;
    float bCrit = 3.0 * sqrt(3.0) * M;

    // Ray relative to BH at origin
    vec3 pos = origin;
    vec3 vel = normalize(dir);

    // Angular momentum direction = orbital plane normal
    vec3 Lvec = cross(pos, vel);
    float L = length(Lvec);
    if (L < 1e-8) {
        // Radial ray
        float r0 = length(pos);
        if (dot(pos, vel) < 0.0) {
            res.color = vec3(0.0);
            res.capture = 1.0;
            res.alpha = 1.0;
        } else {
            res.color = starField(vel);
            res.alpha = 1.0;
        }
        return res;
    }
    vec3 nPlane = Lvec / L;

    // Orthonormal basis in the orbital plane
    vec3 eR = normalize(pos);
    vec3 ePhi = normalize(cross(nPlane, eR));

    // Impact parameter b = |r × k̂| / |k̂ · ê_t-related| for asymptotic rays.
    // For finite camera: b = L / E with E from local null normalization.
    // Using coordinate affine param with |vel|=1 at start:
    // b = |x × v|  (geometric units, asymptotically correct when far)
    float b = L; // since |vel|=1, |pos×vel| = r sinψ = b_asymp when far

    float r = length(pos);
    float u = 1.0 / max(r, 1e-6);

    // Sign of dφ: motion sense in plane
    float phiSign = sign(dot(vel, ePhi));
    if (abs(phiSign) < 0.5) phiSign = 1.0;

    // du/dφ from geometry: du/dφ = −(1/r²) dr/dφ, and dr/dφ = (v·eR)/(v·ePhi/r)
    float vr = dot(vel, eR);
    float vphi = dot(vel, ePhi); // ≈ r dφ/dλ
    // dφ/dλ = vphi/r, dr/dλ = vr → du/dφ = du/dλ / dφ/dλ = (−vr/r²) / (vphi/r) = −vr/(r vphi)
    float dudphi = 0.0;
    if (abs(vphi) > 1e-8) {
        dudphi = -vr / (r * vphi);
    }

    // Quality settings
    int maxSteps = uMaxSteps;
    float dphi = 0.02;
    if (uQuality <= 0) { maxSteps = min(maxSteps, 180); dphi = 0.035; }
    else if (uQuality == 1) { maxSteps = min(maxSteps, 320); dphi = 0.022; }
    else { maxSteps = min(maxSteps, 520); dphi = 0.014; }

    // Adaptive: finer near photon sphere
    float phi = 0.0;
    float totalPhi = 0.0;

    vec3 color = vec3(0.0);
    float transmittance = 1.0;

    // Track previous position for disk plane crossing (y=0 equatorial)
    vec3 prevCartesian = pos;
    float prevY = pos.y;

    bool captured = false;
    bool escaped = false;
    vec3 escapeDir = vel;

    // Current cartesian reconstruction helpers
    // We rotate the initial radius vector around nPlane by φ and scale to r=1/u
    float baseAngle = 0.0; // φ measured from initial eR

    for (int step = 0; step < 512; ++step) {
        if (step >= maxSteps) break;

        // Adaptive step: smaller near photon sphere / horizon
        float rNow = 1.0 / max(u, 1e-6);
        float adapt = 1.0;
        if (rNow < 8.0 * M) adapt = 0.45;
        if (rNow < 4.0 * M) adapt = 0.25;
        if (abs(rNow - rPh) < 0.75 * M) adapt = 0.18;
        float h = dphi * adapt * phiSign;

        // RK4 on (u, w=du/dφ) with w' = 3 M u² − u
        float u0 = u;
        float w0 = dudphi;

        // k1
        float k1u = w0;
        float k1w = 3.0 * M * u0 * u0 - u0;

        // k2
        float u2 = u0 + 0.5 * h * k1u;
        float w2 = w0 + 0.5 * h * k1w;
        float k2u = w2;
        float k2w = 3.0 * M * u2 * u2 - u2;

        // k3
        float u3 = u0 + 0.5 * h * k2u;
        float w3 = w0 + 0.5 * h * k2w;
        float k3u = w3;
        float k3w = 3.0 * M * u3 * u3 - u3;

        // k4
        float u4 = u0 + h * k3u;
        float w4 = w0 + h * k3w;
        float k4u = w4;
        float k4w = 3.0 * M * u4 * u4 - u4;

        u = u0 + (h / 6.0) * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
        dudphi = w0 + (h / 6.0) * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
        phi += h;
        totalPhi += abs(h);

        if (u <= 1e-6) {
            // r → ∞
            escaped = true;
            break;
        }
        if (u >= 1.0 / (rs * 1.001)) {
            captured = true;
            break;
        }

        // Reconstruct Cartesian position by rotating initial pos around plane normal
        float rCur = 1.0 / u;
        // Angle in plane from initial radial direction
        float ang = phi; // integrated φ
        // Rodrigues rotation of (r0_hat * rCur) around nPlane by ang
        vec3 v = eR;
        vec3 rotated = v * cos(ang) + cross(nPlane, v) * sin(ang)
                     + nPlane * dot(nPlane, v) * (1.0 - cos(ang));
        vec3 cur = rotated * rCur;

        // Disk intersection: equatorial plane y=0 (BH spin axis = +Y)
        if (uShowDisk != 0) {
            float y0 = prevY;
            float y1 = cur.y;
            if (y0 * y1 <= 0.0 && abs(y0 - y1) > 1e-10) {
                float t = y0 / (y0 - y1);
                vec3 hit = mix(prevCartesian, cur, clamp(t, 0.0, 1.0));
                float rh = length(vec3(hit.x, 0.0, hit.z));
                // Avoid false hits very near horizon singularity in embedding
                if (rh > uDiskInner * 0.98 && rh < uDiskOuter && rh > rs * 1.05) {
                    float prof = diskProfile(rh, uDiskInner, uDiskOuter);
                    if (prof > 1e-5) {
                        // Temperature proxy
                        float temp = prof * 2.5;

                        // Gravitational redshift for static emitter:
                        //   g = ν_∞ / ν_em = √(1 − rs/r)
                        float g_grav = sqrt(max(1.0 - rs / rh, 1e-4));

                        // Keplerian Doppler: observer at infinity along photon direction
                        // Approximate line-of-sight from orbital velocity direction
                        vec3 phiHat = normalize(vec3(-hit.z, 0.0, hit.x));
                        float vK = keplerSpeed(rh, M);
                        // Animate disk rotation
                        float spin = uTime * 0.35;
                        float cs = cos(spin), ss = sin(spin);
                        // Orbital direction (prograde around +Y)
                        vec3 vOrb = phiHat * vK;

                        // Photon direction at hit (finite difference)
                        vec3 kdir = normalize(cur - prevCartesian);
                        // Special-relativistic Doppler for emitter moving with vOrb
                        // g_doppler = γ (1 − n·β)  for received/emitted with n toward observer along -k for backward ray
                        // Backward ray: we travel against light, so light travels -kdir toward camera
                        vec3 nLight = normalize(-kdir); // direction of light propagation
                        float betaDot = dot(vOrb, nLight);
                        float gamma = 1.0 / sqrt(max(1.0 - vK * vK, 1e-4));
                        float g_doppler = 1.0 / max(gamma * (1.0 - betaDot), 0.05);

                        float g = clamp(g_grav * g_doppler, 0.05, 4.0);

                        // Observed intensity ~ g³ * emissivity for bolometric (Liouville / relativistic beaming)
                        float intensity = prof * g * g * g * 4.5;

                        // Photon-ring boost: rays that have wound significantly near r_ph
                        float windBoost = 1.0 + 2.2 * smoothstep(PI, 3.0 * PI, totalPhi);
                        // Secondary/tertiary image contribution near strong deflection
                        float ring = exp(-pow((rh - rPh * 2.0) / (1.1 * M), 2.0));
                        intensity *= windBoost;

                        vec3 emit = tempToColor(temp * g) * intensity;
                        emit += vec3(1.0, 0.75, 0.45) * ring * 0.55 * g;

                        // Optically thin disk: accumulate with partial opacity
                        float opacity = clamp(0.65 + 0.3 * prof, 0.0, 0.95);
                        // Multiple images: later windings somewhat attenuated
                        opacity *= mix(1.0, 0.65, smoothstep(TWO_PI, 4.0 * PI, totalPhi));

                        color += transmittance * emit * opacity;
                        transmittance *= (1.0 - opacity * 0.85);

                        if (transmittance < 0.02) break;
                    }
                }
            }
        }

        prevCartesian = cur;
        prevY = cur.y;

        // Escape heuristic: far and moving out
        if (rCur > max(length(origin) * 1.8, 80.0 * M) && dudphi * phiSign < 0.0) {
            escaped = true;
            // Escape direction: asymptotic
            vec3 er = normalize(cur);
            vec3 eph = normalize(cross(nPlane, er));
            // From u, du/dφ: direction combination
            escapeDir = normalize(-er * (dudphi) + eph * (1.0)); // rough
            // Better: finite difference
            escapeDir = normalize(cur - prevCartesian);
            break;
        }

        // If potential barrier reflects (turning point already handled by ODE)
        if (totalPhi > 10.0 * PI) {
            // Extreme winding — treat as captured into photon ring darkness / glow
            break;
        }
    }

    if (captured) {
        // Pure black horizon (silhouette); edge glow from nearby lensing handled by nearby rays
        res.color = color; // may already have disk light from front side
        res.alpha = 1.0;
        res.capture = 1.0;
        // Very faint photon-sphere halo contribution for captured near-critical rays
        if (abs(b - bCrit) < 0.35 * M) {
            res.color += vec3(1.0, 0.85, 0.6) * 0.15 * transmittance;
        }
        return res;
    }

    // Background stars lensed by final direction
    vec3 bgDir;
    if (escaped) {
        bgDir = normalize(prevCartesian - origin);
        // Use last segment direction
        bgDir = normalize(escapeDir);
        // Construct asymptotic direction from plane basis
        float rF = 1.0 / max(u, 1e-6);
        vec3 er = normalize(prevCartesian);
        vec3 eph = cross(nPlane, er);
        if (length(eph) > 1e-6) eph = normalize(eph);
        // dr:dφ ratio: dr/dφ = d(1/u)/dφ = - (1/u²) du/dφ
        float drdphi = - (rF * rF) * dudphi;
        bgDir = normalize(er * drdphi + eph * rF * phiSign);
    } else {
        bgDir = normalize(prevCartesian);
    }

    vec3 bg = starField(bgDir);

    // Lensing magnification dimming is complex; apply mild vignetting on high deflection
    float deflect = smoothstep(0.5, 4.0, totalPhi);
    bg *= mix(1.0, 0.75, deflect);

    color += transmittance * bg;

    // Soft glow around critical curve (photon ring halo)
    float crit = exp(-pow((b - bCrit) / (0.12 * M), 2.0));
    color += crit * vec3(1.0, 0.8, 0.55) * 0.45 * transmittance;

    res.color = color;
    res.alpha = 1.0;
    return res;
}

// ---- main ------------------------------------------------------------------

void main() {
    // NDC from UV
    vec2 ndc = vUV * 2.0 - 1.0;
    // Camera ray in world space
    vec3 dir = normalize(
        uCamForward
        + ndc.x * uAspect * uTanHalfFov * uCamRight
        + ndc.y * uTanHalfFov * uCamUp
    );

    GeoResult gr = traceGeodesic(uCamPos, dir, uM);

    // Tone mapping (ACES-ish filmic) for HDR disk brightness
    vec3 c = gr.color;
    c = c * 1.1;
    c = c / (c + vec3(1.0));
    // Gamma
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));

    // Subtle vignette
    float vig = smoothstep(1.4, 0.3, length(ndc * vec2(uAspect, 1.0) * 0.55));
    c *= mix(0.85, 1.0, vig);

    fragColor = vec4(c, 1.0);
}
