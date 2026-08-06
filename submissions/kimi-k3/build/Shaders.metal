// =============================================================================
// Shaders.metal — GPU ray tracing of a Schwarzschild black hole
//
// Physics summary (geometric units, G = c = 1, Schwarzschild radius rs = 1,
// so the mass M = rs/2 = 0.5):
//
//   * Null geodesics are integrated EXACTLY in the Schwarzschild geometry via
//     the relativistic Binet equation for u = 1/r as a function of the
//     orbital-plane angle phi:
//
//         d²u/dphi² = (3/2) rs u² − u
//
//     integrated with classical RK4. Light rays in Schwarzschild are planar,
//     so each pixel's ray lives in the plane spanned by the camera radial
//     direction and the ray direction — we reconstruct full 3D positions from
//     the 2D plane solution. This produces the exact shadow (critical impact
//     parameter b_c = 3√3/2 rs ≈ 2.598 rs), the photon sphere at r = 1.5 rs,
//     gravitational lensing of the background sky, and arbitrarily
//     high-order images of the accretion disk (the "halos"/photon ring).
//
//   * Initial conditions use the camera's local orthonormal tetrad at finite
//     radius: for a ray making angle alpha with the outward radial direction,
//         u'₀ = −u₀ √(1 − rs u₀) cot(alpha)
//
//   * Accretion disk: geometrically thin disk in the equatorial plane, inner
//     edge at the ISCO (r = 6M = 3 rs), Novikov–Thorne-style flux profile
//         F(r) ∝ x⁻³ (1 − x⁻¹ᐟ²),  x = r/r_ISCO
//     (zero-torque inner boundary; peaks at r ≈ 1.36 r_ISCO ≈ 8.2 M).
//     Local disk temperature T ∝ F^{1/4} is mapped to a blackbody color.
//
//   * Relativistic frequency shift of disk photons is EXACT for circular
//     geodesic emitters: with locally measured orbital speed
//         v² = M/(r − 2M)
//     the total shift (gravitational + transverse + longitudinal Doppler) is
//         g = √(1 − rs/r) √(1 − v²) / (1 − v k̂·t̂)
//     Observed intensity picks up Liouville's factor I_obs = g³ I_emit and
//     the blackbody temperature shifts as T_obs = g T.
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Parameters — layout MUST match Params in src/Params.h (128 bytes).
// ---------------------------------------------------------------------------
struct Params {
    float3 camPos;      float aspect;        // camera position (world)
    float3 camRight;    float tanHalfFov;    // camera basis
    float3 camUp;       float time;
    float3 camFwd;      float exposure;
    float3 diskNormal;  float diskIn;        // disk inner radius (ISCO = 3 rs)
    float3 skyNormal;   float diskOut;       // disk outer radius
    uint   maxSteps;    uint  flags;         uint width; uint height;
    float  rs;          float diskBrightness; float tempScale; float farR;
};

// flags bits
#define F_DISK     1u
#define F_SKY      2u
#define F_BEAMING  4u
#define F_REDSHIFT 8u

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------
static float hash13(float3 p) {
    p = fract(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return fract((p.x + p.y) * p.z);
}

static float valueNoise(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    f = f * f * (3.0f - 2.0f * f);
    float n000 = hash13(i);
    float n100 = hash13(i + float3(1,0,0));
    float n010 = hash13(i + float3(0,1,0));
    float n110 = hash13(i + float3(1,1,0));
    float n001 = hash13(i + float3(0,0,1));
    float n101 = hash13(i + float3(1,0,1));
    float n011 = hash13(i + float3(0,1,1));
    float n111 = hash13(i + float3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// Approximate blackbody (Planckian locus) color, T in Kelvin.
// Tanner–Helland style fit, normalized to 0..1.
static float3 blackbody(float T) {
    T = clamp(T, 800.0f, 40000.0f) / 100.0f;
    float r, g, b;
    // red
    if (T <= 66.0f) r = 1.0f;
    else r = clamp(1.29294f * pow(T - 60.0f, -0.1332047f), 0.0f, 1.0f);
    // green
    if (T <= 66.0f) g = clamp((0.39008f * log(T) - 0.63184f), 0.0f, 1.0f);
    else g = clamp(1.12989f * pow(T - 60.0f, -0.0755148f), 0.0f, 1.0f);
    // blue
    if (T >= 66.0f) b = 1.0f;
    else if (T <= 19.0f) b = 0.0f;
    else b = clamp(0.54321f * log(T - 10.0f) - 1.19624f, 0.0f, 1.0f);
    return float3(r, g, b);
}

// ---------------------------------------------------------------------------
// Procedural background sky: starfield + a Milky-Way-like band.
// Sampled with the asymptotic escape direction of the photon, so all lensing
// structure (Einstein rings, duplicated stars, ...) emerges from the ODE.
// ---------------------------------------------------------------------------
static float3 skyColor(float3 d, float3 skyN) {
    float3 col = float3(0.0f);

    // two star layers at different densities/scales
    for (int layer = 0; layer < 2; layer++) {
        float scale = (layer == 0) ? 160.0f : 320.0f;
        float thresh = (layer == 0) ? 0.985f : 0.994f;
        float3 sd = d * scale;
        float3 cell = floor(sd);
        float3 f = fract(sd);
        float rn = hash13(cell);
        if (rn > thresh) {
            float3 sp = float3(hash13(cell + 1.3f), hash13(cell + 2.7f), hash13(cell + 4.1f));
            sp = 0.15f + 0.7f * sp;
            float dist = length(f - sp);
            float bright = (rn - thresh) / (1.0f - thresh);
            float star = smoothstep(0.10f, 0.0f, dist) * (0.35f + 0.65f * bright);
            // slight color variety by temperature
            float temp = hash13(cell + 9.1f);
            float3 sc = mix(float3(1.0f, 0.75f, 0.55f), float3(0.7f, 0.85f, 1.0f), temp);
            col += star * sc * ((layer == 0) ? 1.0f : 0.6f);
        }
    }

    // Milky-Way band: great-circle band with noisy structure and dust lanes
    float band = exp(-14.0f * dot(d, skyN) * dot(d, skyN));
    if (band > 0.01f) {
        float n1 = valueNoise(d * 6.0f);
        float n2 = valueNoise(d * 14.0f + 7.7f);
        float dust = valueNoise(d * 10.0f + 3.1f);
        float glow = band * (0.25f + 0.55f * n1 + 0.25f * n2);
        glow *= (0.45f + 0.55f * smoothstep(0.25f, 0.75f, dust)); // dust lanes
        col += glow * float3(0.65f, 0.60f, 0.72f) * 0.40f;
    }

    // faint deep-space floor so the sky is never pure black
    col += float3(0.012f, 0.014f, 0.020f);
    return col;
}

// ---------------------------------------------------------------------------
// Accretion disk emission at one plane crossing.
//   crossPos : 3D crossing point, rc = |crossPos|
//   uc, upc  : u = 1/r and du/dphi at the crossing
//   radC,tanC: radial / tangential unit vectors of the photon orbital plane
// Returns emitted radiance observed at the camera (after g-factor).
// ---------------------------------------------------------------------------
static float3 diskEmission(float rc, float uc, float upc,
                           float3 radC, float3 tanC,
                           constant Params& P) {
    float rs = P.rs;
    float M  = 0.5f * rs;

    // Local photon direction k̂ in the static orthonormal frame at the crossing.
    // dr/dphi = -u'/u² ; the physical angle beta from the radial direction obeys
    // tan(beta) = r √(1-rs/r) / (dr/dphi)  (metric factor included).
    float fc = sqrt(1.0f - rs / rc);
    float drdphi = -upc / (uc * uc);
    float beta = atan2(rc * fc, drdphi);          // in (0, pi), handles in/out
    float3 khat = cos(beta) * radC + sin(beta) * tanC;

    // Circular geodesic emitter: locally measured orbital speed v² = M/(r-2M)
    float v2 = M / (rc - rs);
    float v  = sqrt(v2);
    float3 tdisk = normalize(cross(P.diskNormal, radC)); // prograde azimuthal
    // NOTE: we integrate rays backward (camera -> source), so the physical
    // photon propagation direction at emission is -khat. The Doppler factor
    // uses the direction from emitter toward observer: delta ~ 1/(1 - v·k_phys)
    float dop = -v * dot(khat, tdisk);

    // Exact g-factor: g = sqrt(1-rs/r) sqrt(1-v²) / (1 - v k̂·t̂)
    float g = 1.0f;
    if (P.flags & F_REDSHIFT) g = fc * sqrt(1.0f - v2);
    if (P.flags & F_BEAMING)  g = g / max(1.0f - dop, 1e-3f);

    // Novikov–Thorne-style flux, zero at ISCO, peak at x = (7/6)² ≈ 1.361
    float x = rc / P.diskIn;
    float F = (1.0f / (x * x * x)) * max(0.0f, 1.0f - rsqrt(x));
    const float FMAX = 0.0567f;                 // normalization at the peak
    float Fn = clamp(F / FMAX, 0.0f, 1.5f);

    // outer edge fade
    Fn *= smoothstep(P.diskOut, P.diskOut * 0.82f, rc);

    // Observed blackbody: T_obs = g T, intensity boosted by g³ (Liouville)
    float T = P.tempScale * pow(Fn, 0.25f) * g;
    float3 bb = blackbody(T);
    float boost = (P.flags & F_BEAMING) ? (g * g * g) : 1.0f;
    return bb * Fn * boost * P.diskBrightness;
}

// ---------------------------------------------------------------------------
// Main ray trace: integrate one photon backward from the camera.
// ---------------------------------------------------------------------------
static float3 traceRay(float3 ro, float3 rd, constant Params& P) {
    float rs = P.rs;
    float r0 = length(ro);
    float3 rhat = ro / r0;
    float cosA = clamp(dot(rd, rhat), -1.0f, 1.0f);
    float3 e1 = rhat;
    float3 e2v = rd - cosA * rhat;
    float sinA = length(e2v);

    // Degenerate radial ray: outward -> sky, inward -> horizon.
    if (sinA < 1e-7f) {
        if (cosA > 0.0f && (P.flags & F_SKY)) return skyColor(rd, P.skyNormal);
        return float3(0.0f);
    }
    float3 e2 = e2v / sinA;

    // Initial conditions in the camera's local tetrad:
    //   u0 = 1/r0,  u'0 = -u0 sqrt(1 - rs u0) cot(alpha)
    float f0 = sqrt(1.0f - rs / r0);
    float u  = 1.0f / r0;
    float up = -u * f0 * (cosA / sinA);

    float phi = 0.0f;
    float3 col = float3(0.0f);
    float3 prevPos = ro;
    float prevS = dot(ro, P.diskNormal);
    float prevU = u, prevUp = up, prevPhi = phi;

    bool escaped = false;
    float3 escDir = rd;

    for (uint i = 0; i < P.maxSteps; i++) {
        float r = 1.0f / u;
        // Adaptive step in phi: fine near the hole, coarse far away.
        float h = clamp(0.010f * pow(r / rs, 1.5f), 0.004f, 0.12f);

        // RK4 on y = (u, u') with u'' = (3/2) rs u² - u
        float k1u = up;
        float k1p = 1.5f * rs * u * u - u;
        float u2 = u + 0.5f * h * k1u, p2 = up + 0.5f * h * k1p;
        float k2u = p2;
        float k2p = 1.5f * rs * u2 * u2 - u2;
        float u3 = u + 0.5f * h * k2u, p3 = up + 0.5f * h * k2p;
        float k3u = p3;
        float k3p = 1.5f * rs * u3 * u3 - u3;
        float u4 = u + h * k3u, p4 = up + h * k3p;
        float k4u = p4;
        float k4p = 1.5f * rs * u4 * u4 - u4;

        float un = u  + (h / 6.0f) * (k1u + 2.0f * k2u + 2.0f * k3u + k4u);
        float pn = up + (h / 6.0f) * (k1p + 2.0f * k2p + 2.0f * k3p + k4p);
        float phin = phi + h;

        if (un <= 1e-6f) { escaped = true; escDir = rd; break; } // numerical guard
        float rn = 1.0f / un;

        // Captured by the horizon.
        if (rn < rs * 1.0005f) break;

        float c = cos(phin), s = sin(phin);
        float3 pos = rn * (c * e1 + s * e2);

        // Escaped to infinity: sample the sky with the asymptotic direction.
        if (pn < 0.0f && rn > P.farR) {
            float drdphi = -pn / (un * un);
            float3 rad = c * e1 + s * e2;
            float3 tang = -s * e1 + c * e2;
            escDir = normalize(drdphi * rad + rn * tang);
            escaped = true;
            break;
        }

        // Accretion-disk plane crossing?
        if (P.flags & F_DISK) {
            float s1 = dot(pos, P.diskNormal);
            if (prevS * s1 < 0.0f) {
                float t = prevS / (prevS - s1);          // linear crossing fraction
                float3 crossPos = mix(prevPos, pos, t);
                float rc = length(crossPos);
                if (rc >= P.diskIn && rc <= P.diskOut) {
                    float uc  = mix(prevU, un, t);
                    float upc = mix(prevUp, pn, t);
                    float phic = mix(prevPhi, phin, t);
                    float cc = cos(phic), ss = sin(phic);
                    float3 radC  = cc * e1 + ss * e2;
                    float3 tanC  = -ss * e1 + cc * e2;
                    col += diskEmission(rc, uc, upc, radC, tanC, P);
                }
            }
            prevS = s1;
        }

        prevPos = pos;
        prevU = u; prevUp = up; prevPhi = phi;
        u = un; up = pn; phi = phin;
    }

    if (!escaped && (P.flags & F_SKY)) {
        // Step budget exhausted (rays hovering near the photon sphere): if the
        // ray is on its way out and clear of the strong field, approximate the
        // escape with its current direction — this keeps the higher-order
        // photon-ring halo bright instead of leaving a dark seam.
        float r = 1.0f / u;
        if (up < 0.0f && r > 2.0f * rs) {
            float c = cos(phi), s = sin(phi);
            float drdphi = -up / (u * u);
            float3 rad = c * e1 + s * e2;
            float3 tang = -s * e1 + c * e2;
            escDir = normalize(drdphi * rad + r * tang);
            escaped = true;
        }
    }
    if (escaped && (P.flags & F_SKY)) col += skyColor(escDir, P.skyNormal);
    return col;
}

// ---------------------------------------------------------------------------
// Compute kernel: one thread per pixel.
// ---------------------------------------------------------------------------
kernel void raytraceKernel(texture2d<float, access::write> outTex [[texture(0)]],
                           constant Params& P [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= P.width || gid.y >= P.height) return;

    float2 uv = (float2(gid) + 0.5f) / float2(P.width, P.height);
    uv = uv * 2.0f - 1.0f;
    uv.y = -uv.y;

    float3 dir = normalize(P.camFwd
                           + uv.x * P.aspect * P.tanHalfFov * P.camRight
                           + uv.y * P.tanHalfFov * P.camUp);

    float3 col = traceRay(P.camPos, dir, P);

    // Tonemap (ACES approximation) + gamma
    col *= P.exposure;
    col = clamp((col * (2.51f * col + 0.03f)) / (col * (2.43f * col + 0.59f) + 0.14f),
                0.0f, 1.0f);
    col = pow(col, float3(1.0f / 2.2f));

    outTex.write(float4(col, 1.0f), gid);
}

// ===========================================================================
// Below: small raster pipelines for the fullscreen quad and the 3D spacetime
// grid (Flamm's paraboloid embedding diagram) + orbiting test-particle marbles.
// ===========================================================================

struct QuadOut {
    float4 pos [[position]];
    float2 uv;
};

vertex QuadOut quadVert(uint vid [[vertex_id]]) {
    // fullscreen triangle
    float2 p = float2((vid << 1) & 2, vid & 2);
    QuadOut o;
    o.pos = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
    o.uv = float2(p.x, 1.0f - p.y);
    return o;
}

fragment float4 quadFrag(QuadOut in [[stage_in]],
                         texture2d<float> tex [[texture(0)]],
                         sampler smp [[sampler(0)]]) {
    return float4(tex.sample(smp, in.uv).rgb, 1.0f);
}

// --- grid / marble pipelines -------------------------------------------------

struct MeshUniforms {
    float4x4 mvp;
    float4 color;      // used by marbles / highlight rings
};

struct LineVertex {
    float3 pos [[attribute(0)]];
    float3 col [[attribute(1)]];
};

struct LineOut {
    float4 pos [[position]];
    float3 col;
};

vertex LineOut lineVert(LineVertex in [[stage_in]],
                        constant MeshUniforms& U [[buffer(1)]]) {
    LineOut o;
    o.pos = U.mvp * float4(in.pos, 1.0f);
    o.col = in.col;
    return o;
}

fragment float4 lineFrag(LineOut in [[stage_in]]) {
    return float4(in.col, 1.0f);
}

// --- spacetime grid surface (Flamm's paraboloid) ----------------------------

struct SurfVertex {
    float3 pos [[attribute(0)]];
};

struct SurfOut {
    float4 pos [[position]];
    float3 world;
};

vertex SurfOut surfVert(SurfVertex in [[stage_in]],
                        constant MeshUniforms& U [[buffer(1)]]) {
    SurfOut o;
    o.pos = U.mvp * float4(in.pos, 1.0f);
    o.world = in.pos;
    return o;
}

// Translucent curved surface with glowing grid lines drawn procedurally.
// Color encodes the Kretschmann curvature scalar K = 12 rs²/r⁶ (sqrt(K) ∝ r⁻³).
fragment float4 surfFrag(SurfOut in [[stage_in]]) {
    float r = length(in.world.xz);
    float theta = atan2(in.world.z, in.world.x);

    // grid lines: rings uniform in log r, spokes uniform in angle
    float rings = log(r) * 6.0f;                 // ~6 rings per e-fold (rs = 1)
    float spokes = theta * (48.0f / (2.0f * M_PI_F));
    float dr = abs(fract(rings + 0.5f) - 0.5f);
    float dt = abs(fract(spokes + 0.5f) - 0.5f);
    float wr = fwidth(rings) + 1e-5f;
    float wt = fwidth(spokes) + 1e-5f;
    float lineR = 1.0f - smoothstep(0.0f, wr * 1.6f, dr);
    float lineT = 1.0f - smoothstep(0.0f, wt * 1.6f, dt);
    float line = max(lineR, lineT);

    float t = clamp(pow(1.0f / r, 3.0f), 0.0f, 1.0f);   // sqrt(Kretschmann)
    float3 cold = float3(0.25f, 0.45f, 0.85f);
    float3 hot  = float3(1.00f, 0.45f, 0.12f);
    float3 base = mix(cold, hot, t);
    float3 col = base * (0.60f + 0.80f * t) * (0.35f + 0.65f * line);
    float alpha = 0.07f + 0.60f * line;
    return float4(col, alpha);
}

struct SphereVertex {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
};

struct SphereOut {
    float4 pos [[position]];
    float3 nrm;
};

vertex SphereOut sphereVert(SphereVertex in [[stage_in]],
                            constant MeshUniforms& U [[buffer(1)]]) {
    SphereOut o;
    o.pos = U.mvp * float4(in.pos, 1.0f);
    o.nrm = in.nrm;
    return o;
}

fragment float4 sphereFrag(SphereOut in [[stage_in]],
                           constant MeshUniforms& U [[buffer(1)]]) {
    float3 L = normalize(float3(0.5f, 0.8f, 0.3f));
    float diff = 0.30f + 0.70f * max(0.0f, dot(normalize(in.nrm), L));
    return float4(U.color.rgb * diff, 1.0f);
}
