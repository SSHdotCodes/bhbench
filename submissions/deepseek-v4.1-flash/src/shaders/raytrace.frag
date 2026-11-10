#version 410 core

// ---------------------------------------------------------------------------
// Schwarzschild black hole ray tracer.
//
// Every pixel launches a null geodesic backwards from a *static* observer and
// integrates the exact Schwarzschild null geodesic equation
//
//     d^2 x / dlambda^2 = -(3/2) r_s h^2 x / r^5 ,   h = |x cross v| = const
//
// with classical RK4 and an adaptive step. Units: G = c = 1, M = 1, so
// r_s = 2, the photon sphere is at r = 3 and the ISCO at r = 6.
// ---------------------------------------------------------------------------

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uRes;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;
uniform float uTime;

uniform float uExposure;
uniform float uGamma;

uniform float uDiskInner;
uniform float uDiskOuter;
uniform float uDiskTempPeak;
uniform float uDiskBrightness;
uniform int   uPhysical;      // 1 = absolute blackbody radiance, 0 = normalised
uniform int   uDiskTexture;   // procedural surface texture (visual only)
uniform float uCompress;      // display compression of the visible-band contrast

uniform sampler1D uBB;
uniform float uBBMinT;
uniform float uBBMaxT;

uniform int   uShowStars;
uniform float uStarBrightness;

uniform int   uShowGrid;      // equatorial coordinate grid instead of the disk
uniform float uGridOuter;
uniform float uGridIntensity;

uniform int   uMaxSteps;
uniform vec2  uJitter;        // sub-pixel offset in pixels
uniform vec2  uViewportOffset; // origin of the active viewport in the FBO

const float RS = 2.0;         // Schwarzschild radius in units of M
const float PI = 3.141592653589793;

// ---------------------------------------------------------------------------
// Hashing / noise
// ---------------------------------------------------------------------------
float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

vec2 hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float hash31(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0, 0, 0));
    float n100 = hash31(i + vec3(1, 0, 0));
    float n010 = hash31(i + vec3(0, 1, 0));
    float n110 = hash31(i + vec3(1, 1, 0));
    float n001 = hash31(i + vec3(0, 0, 1));
    float n101 = hash31(i + vec3(1, 0, 1));
    float n011 = hash31(i + vec3(0, 1, 1));
    float n111 = hash31(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; ++i) {
        s += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Blackbody lookup (absolute visible-band radiance, linear sRGB)
// ---------------------------------------------------------------------------
vec3 blackbody(float T) {
    float t = (log(max(T, 1.0)) - log(uBBMinT)) / (log(uBBMaxT) - log(uBBMinT));
    return texture(uBB, clamp(t, 0.0, 1.0)).rgb;
}

vec3 blackbodyChroma(float T) {
    vec3 c = blackbody(T);
    float m = max(max(c.r, c.g), c.b);
    return m > 1e-20 ? c / m : vec3(1.0);
}

// ---------------------------------------------------------------------------
// Accretion disk: Novikov-Thorne thin disk with zero torque at the inner edge.
//
//   Theta(x) = x^(-3/4) (1 - x^(-1/2))^(1/4) / 0.487871 ,  x = r / r_in
//   T(r)     = T_peak * Theta(x)
//   g        = 1 / (u^t (1 - Omega b_z))       (grav. redshift x Doppler)
//   T_obs    = g T(r)
//
// b_z is L_z/E of the *physical* photon. Because we trace backwards, the
// integrator's L_z has the opposite sign, hence the negation at the call site.
// ---------------------------------------------------------------------------
float luminanceOf(float T) {
    return dot(blackbody(T), vec3(0.2126, 0.7152, 0.0722));
}

vec3 diskRadiance(float r, float phi, float bz, out float Tobs) {
    float x = r / uDiskInner;
    float theta = pow(x, -0.75) * pow(max(1.0 - inversesqrt(x), 0.0), 0.25) / 0.487871;
    float T = uDiskTempPeak * theta;

    float Om = sqrt(0.5 * RS / (r * r * r));          // M = RS/2
    float ut = inversesqrt(max(1.0 - 1.5 * RS / r, 1e-4));
    float g = 1.0 / (ut * (1.0 - Om * bz));
    Tobs = g * T;

    vec3 col;
    if (uPhysical == 1) {
        // Exact: the observed spectrum is a blackbody at T_obs = g T, so the
        // visible-band radiance is simply LUT(T_obs). No extra g^4 factor.
        col = blackbody(Tobs);
    } else {
        // Visible-band radiance relative to the disk's peak temperature. This
        // already contains the Novikov-Thorne flux profile and the full
        // relativistic beaming, because T_obs = g T. uCompress < 1 is a display
        // choice: the true visible-band contrast across the disk exceeds 100:1.
        float rel = luminanceOf(Tobs) / max(luminanceOf(uDiskTempPeak), 1e-9);
        col = blackbodyChroma(Tobs) * pow(max(rel, 0.0), uCompress);
    }

    if (uDiskTexture == 1) {
        // Purely cosmetic: differentially rotating turbulence in the co-moving
        // frame, sheared into trailing logarithmic spirals. Not physics.
        float ang = phi - Om * uTime * 9.0;
        float spiral = ang - 2.2 * log(r);
        float n = fbm(vec3(log(r) * 7.0, spiral * 2.6, uTime * 0.07));
        float n2 = fbm(vec3(log(r) * 19.0, spiral * 7.0, uTime * 0.11));
        col *= mix(0.62, 1.30, clamp(n * 0.75 + n2 * 0.45, 0.0, 1.0));
    }
    return col * uDiskBrightness;
}

// ---------------------------------------------------------------------------
// Sky: procedural star field + Milky Way band, sampled along the escaped ray.
// ---------------------------------------------------------------------------
vec3 starLayer(vec3 d, float scale, float density, float bright, float seed) {
    vec3 ad = abs(d);
    vec2 uv;
    float face;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        uv = d.yz / ad.x;
        face = d.x > 0.0 ? 0.0 : 1.0;
    } else if (ad.y >= ad.z) {
        uv = d.xz / ad.y;
        face = d.y > 0.0 ? 2.0 : 3.0;
    } else {
        uv = d.xy / ad.z;
        face = d.z > 0.0 ? 4.0 : 5.0;
    }
    uv = uv * 0.5 + 0.5;

    vec2 g = uv * scale;
    vec2 cell = floor(g);
    vec2 f = fract(g);

    vec3 col = vec3(0.0);
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 c = cell + vec2(float(i), float(j));
            vec2 key = c + vec2(face * 71.0 + seed, face * 13.0 - seed);
            float rnd = hash21(key).x;
            if (rnd > density) continue;
            vec2 sp = hash21(key + 1.7);
            float d2 = length(f - vec2(float(i), float(j)) - sp);
            float mag = pow(hash21(key + 5.3).y, 9.0);
            float s = smoothstep(0.055, 0.0, d2) * mag;
            float tint = hash21(key + 7.7).x;
            vec3 sc = mix(vec3(0.72, 0.82, 1.0), vec3(1.0, 0.86, 0.62), tint);
            col += s * sc;
        }
    }
    return col * bright;
}

vec3 sky(vec3 d) {
    vec3 col = vec3(0.0);

    // Milky Way: a broad band of dust and unresolved stars.
    vec3 bandN = normalize(vec3(0.32, 0.86, -0.40));
    float band = exp(-pow(dot(d, bandN) / 0.13, 2.0));
    float n1 = fbm(d * 9.0 + 11.0);
    float n2 = fbm(d * 23.0 - 4.0);
    float dust = smoothstep(0.30, 0.80, n1 * 0.75 + n2 * 0.45);
    float milky = band * (0.10 + 1.9 * dust * dust);
    col += milky * vec3(0.26, 0.30, 0.44) * 0.030;

    // Three star layers at different angular scales.
    col += starLayer(d, 90.0, 0.55, 1.0, 0.0) * 1.0;
    col += starLayer(d, 220.0, 0.45, 0.55, 37.0) * 1.0;
    col += starLayer(d, 520.0, 0.35, 0.28, 91.0) * 1.0;

    // Faint blue haze so the sky is never pure black.
    col += vec3(0.0007, 0.0009, 0.0016);

    return col * uStarBrightness;
}

// ---------------------------------------------------------------------------
// Equatorial coordinate grid (lensed). Shows how a regular pattern is warped.
// ---------------------------------------------------------------------------
float gridMask(float r, float phi) {
    const float DR = 2.0;
    const float NPHI = 48.0;
    float ring = abs(fract(r / DR + 0.5) - 0.5) * DR;
    float dphi = 2.0 * PI / NPHI;
    float spoke = abs(fract(phi / dphi + 0.5) - 0.5) * dphi * r;
    float d = min(ring, spoke);
    float w = 0.035 + 0.012 * r;
    return smoothstep(w, 0.0, d);
}

// ---------------------------------------------------------------------------
// Geodesic acceleration
// ---------------------------------------------------------------------------
vec3 accel(vec3 p, float h) {
    float r2 = dot(p, p);
    float r = sqrt(r2);
    return p * (-1.5 * RS * h * h / (r2 * r2 * r));
}

// ---------------------------------------------------------------------------
// Main trace
// ---------------------------------------------------------------------------
void main() {
    vec2 frag = gl_FragCoord.xy - uViewportOffset + uJitter;
    vec2 ndc = (frag / uRes) * 2.0 - 1.0;
    float aspect = uRes.x / uRes.y;

    vec3 dirLocal = normalize(uCamFwd
                              + uCamRight * (ndc.x * uTanHalfFov * aspect)
                              + uCamUp * (ndc.y * uTanHalfFov));

    float r0 = length(uCamPos);
    vec3 rhat = uCamPos / r0;
    float nr = dot(dirLocal, rhat);
    vec3 nt = dirLocal - rhat * nr;

    // Static-observer orthonormal frame -> coordinate 3-velocity.
    vec3 pos = uCamPos;
    vec3 vel = rhat * (nr * sqrt(max(1.0 - RS / r0, 1e-6))) + nt;

    float h = length(cross(pos, vel));
    float vr = dot(vel, rhat);
    float E = sqrt(max(vr * vr + (1.0 - RS / r0) * h * h / (r0 * r0), 1e-12));

    // Physical photon's b_z = L_z / E. The backward ray carries -L_z.
    float bz = -cross(pos, vel).z / E;

    float escapeR = max(r0 * 1.5, 120.0);

    vec3 accum = vec3(0.0);
    bool done = false;

    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(pos);

        if (r < RS * 1.0008) {           // crossed the horizon
            done = true;
            break;
        }
        if (r > escapeR && dot(pos, vel) > 0.0) {
            break;                        // escaped to the sky
        }

        float dl = 0.05 * r * r / (h + 1.0);
        dl = min(dl, 0.15 * r);
        dl = max(dl, 0.002);

        // --- RK4 ---
        vec3 p0 = pos, v0 = vel;
        vec3 a0 = accel(p0, h);
        vec3 p1 = p0 + v0 * (0.5 * dl), v1 = v0 + a0 * (0.5 * dl);
        vec3 a1 = accel(p1, h);
        vec3 p2 = p0 + v1 * (0.5 * dl), v2 = v0 + a1 * (0.5 * dl);
        vec3 a2 = accel(p2, h);
        vec3 p3 = p0 + v2 * dl, v3 = v0 + a2 * dl;
        vec3 a3 = accel(p3, h);

        vec3 pn = p0 + (v0 + 2.0 * v1 + 2.0 * v2 + v3) * (dl / 6.0);
        vec3 vn = v0 + (a0 + 2.0 * a1 + 2.0 * a2 + a3) * (dl / 6.0);

        // --- equatorial plane crossing ---
        if (p0.z * pn.z < 0.0) {
            float t = p0.z / (p0.z - pn.z);
            vec3 cp = mix(p0, pn, t);
            float rc = length(cp);
            float phi = atan(cp.y, cp.x);

            if (uShowGrid == 1) {
                if (rc > RS * 1.02 && rc < uGridOuter) {
                    float m = gridMask(rc, phi);
                    if (m > 0.001) {
                        float fade = smoothstep(uGridOuter, uGridOuter * 0.75, rc);
                        vec3 gc = mix(vec3(0.25, 0.85, 1.0), vec3(0.55, 0.35, 1.0),
                                      clamp(rc / uGridOuter, 0.0, 1.0));
                        accum += gc * m * fade * uGridIntensity;
                        done = true;
                        break;
                    }
                }
            } else if (rc > uDiskInner && rc < uDiskOuter) {
                float Tobs;
                accum += diskRadiance(rc, phi, bz, Tobs);
                done = true;
                break;
            }
        }

        pos = pn;
        vel = vn;
    }

    if (!done) {
        accum += sky(normalize(vel));
    }

    fragColor = vec4(accum, 1.0);
}
