// bh.metal — GPU side of the Kerr black-hole renderer.
//
//   traceFragment     one backward null geodesic per pixel (shared core: kerr_core.h), shading the thin disk,
//                     the spacetime grid / embedding surface and the lensed sky along the way
//   accumulate        progressive supersampling (running mean of jittered frames)
//   bloomDown/Up      multi-scale glare (point-spread function of the "camera")
//   compositeFragment bicubic upscale + bloom + tone mapping + dithering into the drawable
//   skyGenerate       procedural star field + Milky Way cube map (blackbody stars, stored as I and I·T)
#include <metal_stdlib>
using namespace metal;

#include "bh_shared.h"

typedef float real;
typedef float3 vec3;
#define BH_THREAD thread
#include "kerr_core.h"

constant float PI_F = 3.14159265358979f;

// ------------------------------------------------------------------------------------------------
// Hashing and noise
// ------------------------------------------------------------------------------------------------
inline uint hash_u(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline uint hash3i(int3 c) {
    return hash_u(uint(c.x) * 0x8da6b343u ^ hash_u(uint(c.y) * 0xd8163841u ^ hash_u(uint(c.z) * 0xcb1ab31fu)));
}
inline float u01(uint h) { return float(h >> 8) * (1.0f / 16777216.0f); }

inline float3 grad3(int3 c) {
    uint h = hash3i(c);
    return float3(u01(h), u01(hash_u(h ^ 0x68bc21ebu)), u01(hash_u(h ^ 0x02e5be93u))) * 2.0f - 1.0f;
}
inline float gnoise(float3 p) {
    float3 i = floor(p), f = p - i;
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    int3 c = int3(i);
    float n000 = dot(grad3(c + int3(0, 0, 0)), f - float3(0, 0, 0));
    float n100 = dot(grad3(c + int3(1, 0, 0)), f - float3(1, 0, 0));
    float n010 = dot(grad3(c + int3(0, 1, 0)), f - float3(0, 1, 0));
    float n110 = dot(grad3(c + int3(1, 1, 0)), f - float3(1, 1, 0));
    float n001 = dot(grad3(c + int3(0, 0, 1)), f - float3(0, 0, 1));
    float n101 = dot(grad3(c + int3(1, 0, 1)), f - float3(1, 0, 1));
    float n011 = dot(grad3(c + int3(0, 1, 1)), f - float3(0, 1, 1));
    float n111 = dot(grad3(c + int3(1, 1, 1)), f - float3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
inline float fbm(float3 p, int oct) {
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; ++i) {
        s += a * gnoise(p);
        p = p * 2.03f + float3(1.7f, -3.1f, 2.3f);
        a *= 0.5f;
    }
    return s;
}

// ------------------------------------------------------------------------------------------------
// Tables
// ------------------------------------------------------------------------------------------------
// Blackbody colour per unit bolometric radiance (rgb) and luminance per unit bolometric radiance (w).
inline float4 bb_lookup(constant float4* lut, int n, float logT0, float logT1, float T) {
    float lt = log10(max(T, 1.0f));
    float x = (lt - logT0) / (logT1 - logT0) * float(n - 1);
    if (x <= 0.0f) return float4(0.0f);
    if (x >= float(n - 1)) {   // Rayleigh–Jeans tail: visible radiance ∝ T while bolometric ∝ T⁴
        float s = exp10((logT1 - lt) * 3.0f);
        return lut[n - 1] * s;
    }
    int i = int(x);
    return mix(lut[i], lut[i + 1], x - float(i));
}

inline float table_sqrtmap(constant float* tab, int n, float x, float x0, float x1) {
    float u = sqrt(clamp((x - x0) / (x1 - x0), 0.0f, 1.0f));
    float f = u * float(n - 1);
    int i = min(int(f), n - 2);
    return mix(tab[i], tab[i + 1], f - float(i));
}

// Co-rotating turbulence: log-normal brightness fluctuations advected with the local Keplerian flow and
// continually regenerated (two cross-faded generations of lifetime TC), so the pattern is statistically
// stationary for all times while each generation is sheared by differential rotation.
inline float disk_turbulence(float r, float phi, float t, float a, float amp) {
    float om = 1.0f / (r * sqrt(r) + a);
    const float TC = 70.0f;
    float acc = 0.0f, w2 = 0.0f;
    for (int p = 0; p < 2; ++p) {
        float c = t / TC + 0.5f * float(p);
        float s = fract(c);
        float gen = floor(c) * 2.0f + float(p);
        float w = 1.0f - abs(2.0f * s - 1.0f);
        float ang = phi - om * (s * TC);
        float3 q = float3(cos(ang) * 2.4f, sin(ang) * 2.4f, log(r) * 6.0f) + float3(17.3f, -31.1f, 11.7f) * gen;
        acc += w * fbm(q, 5);
        w2 += w * w;
    }
    acc *= rsqrt(max(w2, 1e-4f));
    return exp(amp * 3.2f * acc - 0.5f * amp * amp);
}

// ------------------------------------------------------------------------------------------------
// Grid lines with footprint filtering (no aliasing when lines are thinner than a pixel)
// ------------------------------------------------------------------------------------------------
inline float line_cov(float d, float hw, float fp) {
    float we = max(hw, 0.5f * fp);
    return (1.0f - smoothstep(we - 0.5f * fp, we + 0.5f * fp, d)) * (hw / we);
}
inline float periodic_cov(float x, float spacing, float hw, float fp) {
    float d = abs(fract(x / spacing + 0.5f) - 0.5f) * spacing;
    float c = line_cov(d, hw, fp);
    return mix(c, 2.0f * hw / spacing, smoothstep(0.25f * spacing, 0.7f * spacing, fp));
}

constant float3 kSpecialColor[BH_NUM_SPECIAL] = {
    float3(1.00f, 0.12f, 0.10f),   // event horizon
    float3(0.95f, 0.25f, 1.00f),   // ergosphere (equator, r = 2M)
    float3(1.00f, 0.85f, 0.25f),   // prograde photon orbit
    float3(0.85f, 0.70f, 0.25f),   // retrograde photon orbit
    float3(1.00f, 0.50f, 0.08f),   // ISCO
};

inline void shade_grid(thread float3& color, thread float& trans, float rr, float phi, float fp,
                       constant float* special, float gain, float spacing) {
    float hw = 0.018f + 0.0011f * rr;
    float cr = periodic_cov(rr, spacing, hw, fp);
    float rs = max(rr, 0.5f);
    float cs = periodic_cov(phi, 2.0f * PI_F / 24.0f, hw / rs, fp / rs);
    float cov = max(cr, cs);
    float3 col = float3(0.28f, 0.60f, 1.00f) * cov;
    for (int i = 0; i < BH_NUM_SPECIAL; ++i) {
        float c = line_cov(abs(rr - special[i]), hw * 1.7f, fp);
        col = mix(col, kSpecialColor[i] * 1.6f, c);
        cov = max(cov, c);
    }
    color += trans * col * gain;
    trans *= 1.0f - 0.8f * cov;
}

// ------------------------------------------------------------------------------------------------
// The visitor: shading of every surface the backward ray meets
// ------------------------------------------------------------------------------------------------
struct ShadeVisitor {
    constant FrameUniforms* U;
    constant float* flux;
    constant float4* lut;
    constant float* funnelTab;
    float3 color;
    float trans;
    float firstR, firstG;
    int firstOrder;

    float funnelHeight(float R) const {
        return table_sqrtmap(funnelTab, U->funnelCount, R, U->funnelR0, U->funnelR1);
    }

    // cos of the angle between the ray and the disk normal, measured by the local ZAMO at the equator
    float plane_cosi(thread const KerrRay& k, float r) const {
        float a2 = k.a * k.a;
        float delta = r * r - 2.0f * k.m * r + a2;
        float aeq = (r * r + a2) * (r * r + a2) - a2 * delta;
        float alpha = sqrt(max(r * r * delta / aeq, 1e-12f));
        float omega = 2.0f * k.m * k.a * r / aeq;
        return clamp(sqrt(max(k.Q, 0.0f)) * alpha / (r * max(k.E - omega * k.L, 1e-6f)), 0.0f, 1.0f);
    }

    bool onPlane(thread const KerrRay& k, thread const RayState& c, int crossing, float path, bool goingDown) {
        constant FrameUniforms& u = *U;
        float r = 1.0f / c.rho;
        float3 nf = kerr_rotz(c.n, c.phir);
        float phi = atan2(nf.y, nf.x);

        if (u.gridMode == BH_GRID_PLANE && r < u.gridRmax && r > u.rPlus * 0.98f) {
            float fp = u.pixelAngle * path / max(plane_cosi(k, r), 0.03f);
            shade_grid(color, trans, r, phi, fp, u.specialPlane, u.gridGain, 2.0f);
        }

        if ((u.flags & BH_FLAG_DISK) && r >= u.diskRin && r <= u.rDiskOut) {
            float a = u.diskSpin;
            // Redshift factor g = ν_obs / ν_emit for gas on circular Keplerian orbits: g = 1 / (u^t (E − ΩL)).
            float gTrue = 1.0f;
            if (u.flags & BH_FLAG_GR) {
                float om = kerr_orbit_omega(a, r), ut = kerr_orbit_ut(a, r);
                gTrue = 1.0f / (ut * max(k.E - om * k.L, 1e-6f));
            }
            float g = (u.flags & BH_FLAG_REDSHIFT) ? gTrue : 1.0f;
            float F = table_sqrtmap(flux, u.fluxCount, r, u.diskRin, u.rDiskOut);   // F/F_max (Page–Thorne)
            if (u.flags & BH_FLAG_TURBULENCE) F *= disk_turbulence(r, phi, u.simTime - c.tdel, a, u.turbAmp);
            float T = u.diskTpeak * sqrt(sqrt(max(F, 0.0f)));   // σT⁴ = F
            // A blackbody seen with shift g is a blackbody at gT: I_ν,obs = g³ I_ν,emit(ν/g) = B_ν(gT).
            float Tobs = g * T;
            float4 bb = bb_lookup(lut, u.lutCount, u.lutLogT0, u.lutLogT1, Tobs);
            float x = Tobs / u.diskTpeak;
            float I = x * x * x * x;   // bolometric radiance in units of σT_peak⁴/π
            float limb = 1.0f;
            if (u.flags & BH_FLAG_LIMB) {   // Chandrasekhar electron-scattering atmosphere, flux-normalised
                // emission angle in the gas rest frame: cos = p^(θ)/p^(t) = (√Q/r) / (1/g)
                float mu = clamp(gTrue * sqrt(max(k.Q, 0.0f)) / r, 0.0f, 1.0f);
                limb = 0.4213f * (1.0f + 2.06f * mu);
            }
            float3 em = bb.rgb * (I * limb * u.diskExposure);
            if (u.flags & BH_FLAG_ORDER) {
                float y = dot(em, float3(0.2126f, 0.7152f, 0.0722f));
                float3 tint = crossing == 1 ? float3(1.0f, 0.55f, 0.15f)
                            : crossing == 2 ? float3(0.15f, 0.85f, 1.0f)
                                            : float3(1.0f, 0.2f, 0.9f);
                em = tint * (y * 1.6f);
            }
            if (firstOrder == 0) { firstR = r; firstG = gTrue; firstOrder = crossing; }
            // outer truncation: the disk thins out over the last 30% in radius (emission and opacity taper)
            float edge = 1.0f - smoothstep(0.7f * u.rDiskOut, u.rDiskOut, r);
            float op = u.diskOpacity * edge;
            color += trans * op * em;
            trans *= 1.0f - op;
        }
        return trans < 0.002f;
    }

    bool onFunnel(thread const KerrRay& k, thread const RayState& c, thread const RayState& s0,
                  thread const RayState& s1, float path) {
        constant FrameUniforms& u = *U;
        float r = 1.0f / c.rho;
        float nz = c.n.z;
        float R = sqrt(max(0.0f, 1.0f - nz * nz)) * r;
        if (R < u.funnelR0 || R > u.funnelR1) return false;
        float3 nf = kerr_rotz(c.n, c.phir);
        float phi = atan2(nf.y, nf.x);
        float3 p0 = kerr_rotz(s0.n, s0.phir) / s0.rho;
        float3 p1 = kerr_rotz(s1.n, s1.phir) / max(s1.rho, 1e-4f);
        float3 dir = normalize(p1 - p0);
        float dR = 0.02f * max(R, 1.0f);
        float dZ = (funnelHeight(min(R + dR, u.funnelR1)) - funnelHeight(max(R - dR, u.funnelR0))) /
                   (min(R + dR, u.funnelR1) - max(R - dR, u.funnelR0));
        float2 radial = nf.xy / max(length(nf.xy), 1e-6f);
        float3 N = normalize(float3(-dZ * radial.x, -dZ * radial.y, 1.0f));
        float cosi = abs(dot(dir, N));
        float fp = u.pixelAngle * path / max(cosi, 0.03f);
        // faint translucent membrane (brighter at grazing incidence) so the shape reads in 3-D
        float fres = 0.05f + 0.55f * pow(1.0f - cosi, 4.0f);
        color += trans * float3(0.10f, 0.22f, 0.45f) * (u.gridGain * u.funnelSurface * fres);
        trans *= 1.0f - 0.12f * u.funnelSurface * fres;
        shade_grid(color, trans, R, phi, fp, u.specialFunnel, u.gridGain, 2.0f);
        return trans < 0.002f;
    }
};

// ------------------------------------------------------------------------------------------------
// Full-screen triangle
// ------------------------------------------------------------------------------------------------
struct VSOut {
    float4 position [[position]];
};

vertex VSOut fullscreenVertex(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    VSOut o;
    o.position = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
    return o;
}

// ------------------------------------------------------------------------------------------------
// Ray tracing pass
// ------------------------------------------------------------------------------------------------
fragment float4 traceFragment(VSOut in [[stage_in]],
                              constant FrameUniforms& U [[buffer(0)]],
                              constant float* flux [[buffer(1)]],
                              constant float4* lut [[buffer(2)]],
                              constant float* funnelTab [[buffer(3)]],
                              texturecube<float> sky [[texture(0)]]) {
    constexpr sampler skySampler(filter::linear, mip_filter::linear, address::clamp_to_edge, max_anisotropy(8));

    float2 pix = in.position.xy + U.jitter;
    float2 ndc = float2(pix.x / U.resolution.x * 2.0f - 1.0f, 1.0f - pix.y / U.resolution.y * 2.0f);
    float3 dl = float3(ndc.x * U.tanHalfFov * U.aspect, ndc.y * U.tanHalfFov, 1.0f);

    CamFrame cam;
    cam.right = U.camRight; cam.up = U.camUp; cam.fwd = U.camFwd; cam.beta = U.camBeta; cam.gamma = U.camGamma;
    cam.r = U.camR; cam.sinT = U.camSinT; cam.cosT = U.camCosT; cam.sinP = U.camSinP; cam.cosP = U.camCosP;
    cam.Sigma = U.camSigma; cam.Delta = U.camDelta; cam.Akerr = U.camA;
    cam.alpha = U.camAlpha; cam.omega = U.camOmega; cam.varpi = U.camVarpi;

    KerrRay k;
    RayState s;
    kerr_init_ray(cam, U.spin, U.mass, dl, k, s);

    // Analytic shadow boundary: evaluated in uniform control flow so screen-space derivatives are valid.
    float shadowS = 0.0f, shadowW = 1.0f;
    bool wantShadow = (U.flags & BH_FLAG_SHADOW) != 0 && (U.flags & BH_FLAG_GR) != 0;
    if (wantShadow) {
        shadowS = kerr_shadow_fn(U.spin, k.L / k.E, k.Q / (k.E * k.E));
        shadowS = sign(shadowS) * sqrt(abs(shadowS));   // better-conditioned contour
    }
    shadowW = fwidth(shadowS);

    TraceCfg cfg;
    cfg.rhoHorizon = U.rhoHorizon;
    cfg.rhoCapture = U.rhoCapture;
    cfg.rhoEscape = 0.0f;
    cfg.invStepAng = 1.0f / U.stepAng;
    cfg.invStepRho = 1.0f / U.stepRho;
    cfg.invStepPhi = 1.0f / U.stepPhi;
    cfg.funnelRho = U.funnelRho;
    cfg.maxSteps = U.maxSteps;
    cfg.wantTime = (U.flags & BH_FLAG_TURBULENCE) ? 1 : 0;

    ShadeVisitor vis;
    vis.U = &U;
    vis.flux = flux;
    vis.lut = lut;
    vis.funnelTab = funnelTab;
    vis.color = float3(0.0f);
    vis.trans = 1.0f;
    vis.firstR = -1.0f;
    vis.firstG = 0.0f;
    vis.firstOrder = 0;

    TraceResult tr = kerr_trace(k, s, cfg, vis);

    if (U.flags & BH_FLAG_DEBUG) {
        if (tr.status == BH_TRACE_ESCAPED) return float4(float(tr.status), tr.dir);
        return float4(float(tr.status), vis.firstR, vis.firstG, float(tr.crossings));
    }

    // Lensed sky: sample in uniform control flow with explicit gradients of the sky direction, which
    // prefilters the star field correctly even where lensing magnifies or compresses it strongly.
    bool escaped = tr.status == BH_TRACE_ESCAPED;
    float3 dir = tr.dir;
    float3 ddx = dfdx(dir), ddy = dfdy(dir);
    float gl = max(length(ddx), length(ddy));
    if (gl > 0.25f) { ddx *= 0.25f / gl; ddy *= 0.25f / gl; }
    float4 sk = sky.sample(skySampler, dir, gradientcube(ddx, ddy));
    float3 col = vis.color;
    if (escaped && (U.flags & BH_FLAG_SKY)) {
        float I = sk.x;
        float T = sk.y / max(sk.x, 1e-12f) * 1.0e4f;
        // Star light reaches the camera shifted by g = ν_cam/ν_∞ = 1/E (gravitational blueshift for a hovering
        // camera, plus Doppler/aberration for a moving one); stars are blackbodies, so the shifted light is
        // B(gT) scaled by g⁴ bolometrically.
        float g = (U.flags & BH_FLAG_REDSHIFT) ? 1.0f / max(k.E, 1e-4f) : 1.0f;
        float4 bT = bb_lookup(lut, U.lutCount, U.lutLogT0, U.lutLogT1, T);
        float4 bgT = bb_lookup(lut, U.lutCount, U.lutLogT0, U.lutLogT1, g * T);
        float g2 = g * g;
        col += vis.trans * bgT.rgb * (I * g2 * g2 / max(bT.w, 1e-12f) * U.skyGain * U.exposure);
    }
    if (wantShadow) {
        float line = 1.0f - smoothstep(0.6f, 1.6f, abs(shadowS) / max(shadowW, 1e-6f));
        col = mix(col, float3(0.1f, 1.0f, 0.35f) * max(U.exposure, 0.3f), line * 0.85f);
    }
    return float4(col, 1.0f);
}

// ------------------------------------------------------------------------------------------------
// Progressive accumulation
// ------------------------------------------------------------------------------------------------
kernel void accumulate(texture2d<float, access::read> cur [[texture(0)]],
                       texture2d<float, access::read_write> acc [[texture(1)]],
                       constant float& weight [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= acc.get_width() || gid.y >= acc.get_height()) return;
    float4 c = cur.read(gid);
    float4 a = weight >= 1.0f ? c : mix(acc.read(gid), c, weight);
    acc.write(a, gid);
}

// ------------------------------------------------------------------------------------------------
// Bloom (Jimenez 2014 13-tap downsample, tent upsample)
// ------------------------------------------------------------------------------------------------
kernel void bloomDown(texture2d<float, access::sample> src [[texture(0)]],
                      texture2d<float, access::write> dst [[texture(1)]],
                      uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= dst.get_width() || gid.y >= dst.get_height()) return;
    constexpr sampler ls(filter::linear, address::clamp_to_edge);
    float2 uv = (float2(gid) + 0.5f) / float2(dst.get_width(), dst.get_height());
    float2 t = 1.0f / float2(src.get_width(), src.get_height());
    float3 A = src.sample(ls, uv + t * float2(-2, -2)).rgb;
    float3 B = src.sample(ls, uv + t * float2(0, -2)).rgb;
    float3 C = src.sample(ls, uv + t * float2(2, -2)).rgb;
    float3 D = src.sample(ls, uv + t * float2(-1, -1)).rgb;
    float3 E = src.sample(ls, uv + t * float2(1, -1)).rgb;
    float3 F = src.sample(ls, uv + t * float2(-2, 0)).rgb;
    float3 G = src.sample(ls, uv).rgb;
    float3 H = src.sample(ls, uv + t * float2(2, 0)).rgb;
    float3 I = src.sample(ls, uv + t * float2(-1, 1)).rgb;
    float3 J = src.sample(ls, uv + t * float2(1, 1)).rgb;
    float3 K = src.sample(ls, uv + t * float2(-2, 2)).rgb;
    float3 L = src.sample(ls, uv + t * float2(0, 2)).rgb;
    float3 M = src.sample(ls, uv + t * float2(2, 2)).rgb;
    float3 c = (D + E + I + J) * 0.125f + (A + C + K + M) * 0.03125f + (B + F + H + L) * 0.0625f + G * 0.125f;
    dst.write(float4(min(c, float3(6.0e4f)), 1.0f), gid);
}

kernel void bloomUp(texture2d<float, access::sample> low [[texture(0)]],
                    texture2d<float, access::read> cur [[texture(1)]],
                    texture2d<float, access::write> dst [[texture(2)]],
                    uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= dst.get_width() || gid.y >= dst.get_height()) return;
    constexpr sampler ls(filter::linear, address::clamp_to_edge);
    float2 uv = (float2(gid) + 0.5f) / float2(dst.get_width(), dst.get_height());
    float2 t = 1.0f / float2(low.get_width(), low.get_height());
    float3 s = low.sample(ls, uv + t * float2(-1, -1)).rgb + low.sample(ls, uv + t * float2(1, -1)).rgb +
               low.sample(ls, uv + t * float2(-1, 1)).rgb + low.sample(ls, uv + t * float2(1, 1)).rgb;
    s += 2.0f * (low.sample(ls, uv + t * float2(0, -1)).rgb + low.sample(ls, uv + t * float2(0, 1)).rgb +
                 low.sample(ls, uv + t * float2(-1, 0)).rgb + low.sample(ls, uv + t * float2(1, 0)).rgb);
    s += 4.0f * low.sample(ls, uv).rgb;
    dst.write(float4(cur.read(gid).rgb + s * (1.0f / 16.0f), 1.0f), gid);
}

// ------------------------------------------------------------------------------------------------
// Composite: bicubic upscale, bloom, tone mapping, sRGB encoding, dithering
// ------------------------------------------------------------------------------------------------
inline float3 sample_catmull_rom(texture2d<float> tex, float2 uv) {
    constexpr sampler ls(filter::linear, address::clamp_to_edge);
    float2 size = float2(tex.get_width(), tex.get_height());
    float2 sp = uv * size;
    float2 t1 = floor(sp - 0.5f) + 0.5f;
    float2 f = sp - t1;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 t0 = (t1 - 1.0f) / size, t3 = (t1 + 2.0f) / size, t12 = (t1 + w2 / w12) / size;
    float3 r = tex.sample(ls, float2(t0.x, t0.y)).rgb * (w0.x * w0.y) + tex.sample(ls, float2(t12.x, t0.y)).rgb * (w12.x * w0.y) +
               tex.sample(ls, float2(t3.x, t0.y)).rgb * (w3.x * w0.y) + tex.sample(ls, float2(t0.x, t12.y)).rgb * (w0.x * w12.y) +
               tex.sample(ls, float2(t12.x, t12.y)).rgb * (w12.x * w12.y) + tex.sample(ls, float2(t3.x, t12.y)).rgb * (w3.x * w12.y) +
               tex.sample(ls, float2(t0.x, t3.y)).rgb * (w0.x * w3.y) + tex.sample(ls, float2(t12.x, t3.y)).rgb * (w12.x * w3.y) +
               tex.sample(ls, float2(t3.x, t3.y)).rgb * (w3.x * w3.y);
    return max(r, float3(0.0f));
}

inline float3 tm_aces(float3 x) { return clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f); }

// Lupton et al. (2004) colour-preserving arcsinh stretch, the standard way astronomers display high-dynamic-
// range images; over-exposed values roll off towards white instead of clipping per channel.
inline float3 tm_asinh(float3 c, float beta) {
    float I = (c.r + c.g + c.b) * (1.0f / 3.0f);
    if (I <= 1e-12f) return float3(0.0f);
    c *= asinh(beta * I) / (asinh(beta) * I);
    float m = max(c.r, max(c.g, c.b));
    if (m > 1.0f) {
        float w = 1.0f - 1.0f / m;
        c = mix(c / m, float3(1.0f), 0.9f * pow(w, 1.3f));
    }
    return c;
}

inline float srgb_encode(float x) {
    x = clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * pow(x, 1.0f / 2.4f) - 0.055f;
}

fragment float4 compositeFragment(VSOut in [[stage_in]],
                                  texture2d<float> hdr [[texture(0)]],
                                  texture2d<float> bloom [[texture(1)]],
                                  constant PostUniforms& P [[buffer(0)]]) {
    constexpr sampler ls(filter::linear, address::clamp_to_edge);
    float2 uv = in.position.xy / P.outSize;
    float3 c = sample_catmull_rom(hdr, uv);
    if (P.bloomOn) c = mix(c, bloom.sample(ls, uv).rgb * P.bloomNorm, P.bloomStrength);
    if (P.tonemap == 0) c = tm_asinh(c, P.stretch);
    else if (P.tonemap == 1) c = tm_aces(c);
    else c = clamp(c, 0.0f, 1.0f);
    // triangular dither in the encoded domain
    uint2 ip = uint2(in.position.xy);
    uint h = hash_u(ip.x * 1973u + ip.y * 9277u + uint(P.frameIndex) * 26699u);
    float d = (u01(h) + u01(hash_u(h)) - 1.0f) / 255.0f;
    float3 o = float3(srgb_encode(c.r), srgb_encode(c.g), srgb_encode(c.b)) + d;
    return float4(o, 1.0f);
}

// ------------------------------------------------------------------------------------------------
// Procedural sky: blackbody stars (Euclidean number counts N(>F) ∝ F^(−3/2), concentrated to a galactic
// plane) + diffuse Milky Way with dust lanes.  Stored as (I, I·T/10⁴) so it can be Doppler-shifted.
// ------------------------------------------------------------------------------------------------
inline float3 cube_dir(uint face, float sc, float tc) {
    switch (face) {
        case 0: return float3(1.0f, -tc, -sc);
        case 1: return float3(-1.0f, -tc, sc);
        case 2: return float3(sc, 1.0f, tc);
        case 3: return float3(sc, -1.0f, -tc);
        case 4: return float3(sc, -tc, 1.0f);
        default: return float3(-sc, -tc, -1.0f);
    }
}

kernel void skyGenerate(texturecube<float, access::write> out [[texture(0)]],
                        constant SkyParams& P [[buffer(0)]],
                        uint3 gid [[thread_position_in_grid]]) {
    int N = P.size;
    if (int(gid.x) >= N || int(gid.y) >= N || gid.z >= 6) return;
    float sc = (float(gid.x) + 0.5f) / float(N) * 2.0f - 1.0f;
    float tc = (float(gid.y) + 0.5f) / float(N) * 2.0f - 1.0f;
    float3 d = normalize(cube_dir(gid.z, sc, tc));
    float3 g = float3(dot(P.galX, d), dot(P.galY, d), dot(P.galZ, d));
    float b = asin(clamp(g.z, -1.0f, 1.0f));
    float l = atan2(g.y, g.x);

    // --- diffuse galactic light
    float band = exp(-b * b / (2.0f * 0.085f * 0.085f)) * (0.30f + 0.70f * exp(-l * l / (2.0f * 0.95f * 0.95f)));
    float bulge = exp(-(l * l + 2.2f * b * b) / (2.0f * 0.23f * 0.23f));
    float dn = fbm(g * 4.2f + float3(3.1f, 1.7f, -2.2f), 6);
    float lanes = clamp(1.25f - 2.6f * max(dn + 0.05f, 0.0f), 0.08f, 1.0f);
    float ext = mix(1.0f, lanes, exp(-b * b / (2.0f * 0.055f * 0.055f)));
    float clump = max(0.0f, 0.65f + 1.1f * fbm(g * 19.0f + float3(-5.3f, 2.9f, 7.7f), 4));
    float Imw = P.milkyWay * (0.9f * band + 1.7f * bulge) * ext * clump;
    float Tmw = mix(5600.0f, 4300.0f, bulge / (band + bulge + 1e-4f)) * mix(0.72f, 1.0f, ext);
    Imw += P.milkyWay * 0.012f;   // faint isotropic background of unresolved galaxies
    float I = Imw, IT = Imw * Tmw * 1e-4f;

    // --- stars: one candidate per unit cell in a thin spherical shell of radius G
    float G = P.starGrid;
    float texAng = 2.0f / float(N);
    float sig = 0.55f * texAng;
    float norm = texAng * texAng / (2.0f * PI_F * sig * sig);
    float3 p = d * G;
    int3 c0 = int3(floor(p));
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int3 cc = c0 + int3(dx, dy, dz);
                uint h = hash3i(cc);
                float3 sp = float3(cc) + float3(u01(h), u01(hash_u(h ^ 0x1b873593u)), u01(hash_u(h ^ 0xcc9e2d51u)));
                float len = length(sp);
                if (abs(len - G) > 0.5f) continue;
                float3 sd = sp / len;
                float sb = dot(P.galZ, sd);
                float prob = P.starProb * (0.35f + 2.8f * exp(-abs(sb) / 0.22f));
                if (u01(hash_u(h ^ 0x85ebca6bu)) > prob) continue;
                float uf = max(u01(hash_u(h ^ 0xc2b2ae35u)), 2e-4f);
                float fluxS = P.starFlux * pow(uf, -2.0f / 3.0f);
                float tsel = u01(hash_u(h ^ 0x27d4eb2fu)), tr = u01(hash_u(h ^ 0x165667b1u));
                float Ts = tsel < 0.06f ? mix(11000.0f, 30000.0f, tr * tr)
                         : tsel < 0.30f ? mix(6200.0f, 11000.0f, tr)
                         : tsel < 0.72f ? mix(4800.0f, 6200.0f, tr)
                                        : mix(3000.0f, 4800.0f, tr);
                float3 dd = d - sd;
                float w = exp(-dot(dd, dd) / (2.0f * sig * sig)) * norm * fluxS;
                I += w;
                IT += w * Ts * 1e-4f;
            }
    out.write(float4(I, IT, 0.0f, 0.0f), uint2(gid.xy), gid.z);
}

// Self-test of the cube-face convention: every texel stores (face, index); sampling it back at the same
// direction with nearest filtering must return the same pair.
kernel void cubeWriteTest(texturecube<float, access::write> out [[texture(0)]], uint3 gid [[thread_position_in_grid]]) {
    uint N = out.get_width();
    if (gid.x >= N || gid.y >= N || gid.z >= 6) return;
    out.write(float4(float(gid.z), float(gid.y * N + gid.x), 0, 0), uint2(gid.xy), gid.z);
}
kernel void cubeReadTest(texturecube<float> cube [[texture(0)]], device float2* result [[buffer(0)]],
                         uint3 gid [[thread_position_in_grid]]) {
    constexpr sampler ns(filter::nearest, address::clamp_to_edge);
    uint N = cube.get_width();
    if (gid.x >= N || gid.y >= N || gid.z >= 6) return;
    float sc = (float(gid.x) + 0.5f) / float(N) * 2.0f - 1.0f;
    float tc = (float(gid.y) + 0.5f) / float(N) * 2.0f - 1.0f;
    float4 v = cube.sample(ns, normalize(cube_dir(gid.z, sc, tc)));
    result[(gid.z * N + gid.y) * N + gid.x] = v.xy;
}
