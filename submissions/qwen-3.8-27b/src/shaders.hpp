#pragma once
// Metal Shading Language source (compiled at runtime).
// Units: G = c = 1, black hole mass M = 1.
//   event horizon r_s = 2, photon sphere r = 3, ISCO r = 6.

namespace shaders {

inline const char *MSL = R"metal(
#include <metal_stdlib>
using namespace metal;

// ----------------------------------------------------------------- uniforms
struct URT {
    float cam_x, cam_y, cam_z, cam_r;
    float fwd_x, fwd_y, fwd_z, tan_fov;
    float rgt_x, rgt_y, rgt_z, aspect;
    float up_x,  up_y,  up_z,  time;
};

struct UMesh {
    float4x4 proj;
    float4x4 view;
    float time, pad0, pad1, pad2;
};

// ----------------------------------------------------------------- hashing
static uint hashU(uint3 p) {
    p = (p << 13u) ^ (p >> 7u);
    p = p + (p << 15u);
    p = p ^ (p >> 12u);
    p = p + (p << 13u);
    return p.x ^ (p.y * 1973u) ^ (p.z * 9277u);
}

static float hashF(uint3 p) {
    return float(hashU(p)) * (1.0f / 4294967296.0f);
}

static float noise3(float3 p) {
    uint3 i = uint3(floor(p));
    float3 f = p - float3(i);
    float3 u = f * f * (3.0f - 2.0f * f);
    float c00 = hashF(i);
    float c10 = hashF(i + uint3(1, 0, 0));
    float c01 = hashF(i + uint3(0, 1, 0));
    float c11 = hashF(i + uint3(1, 1, 0));
    float c02 = hashF(i + uint3(0, 0, 1));
    float c12 = hashF(i + uint3(1, 0, 1));
    float c03 = hashF(i + uint3(0, 1, 1));
    float c13 = hashF(i + uint3(1, 1, 1));
    float x0 = mix(c00, c10, u.x);
    float x1 = mix(c01, c11, u.x);
    float x2 = mix(c02, c12, u.x);
    float x3 = mix(c03, c13, u.x);
    return mix(mix(x0, x1, u.y), mix(x2, x3, u.y), u.z);
}

static float fbm3(float3 p) {
    return 0.55f * noise3(p) + 0.30f * noise3(p * 2.13f + 13.7f)
         + 0.15f * noise3(p * 4.41f + 41.2f);
}

// ----------------------------------------------------------------- color
static float3 blackbody(float T) {
    float t = clamp(T * 0.01f, 1.9f, 400.0f);
    float r, g, b;
    r = (t <= 66.0f) ? 255.0f : 329.698727446f * pow(t - 60.0f, -0.1332047592f);
    g = (t <= 66.0f) ? 99.4708025861f * log(max(t, 2.0f)) - 161.1195681661f
                     : 288.1221695283f * pow(t - 60.0f, -0.0755148492f);
    b = (t >= 66.0f) ? 255.0f
                     : ((t <= 19.0f) ? 0.0f
                                     : 138.5177312231f * log(t - 10.0f) - 305.0447927307f);
    return clamp(float3(r, g, b), 0.0f, 255.0f) / 255.0f;
}

static float3 aces(float3 x) {
    return clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f),
                 0.0f, 1.0f);
}

// ------------------------------------------------------- background sky
static float3 starLayer(float3 n, float scale, float t, float dens, float gain) {
    float3 p = n * scale;
    uint3 id = uint3(floor(p));
    float3 f = p - float3(id);
    float h0 = hashF(id);
    if (h0 > dens) return float3(0.0f);
    float3 sp = float3(hashF(id + uint3(11, 0, 0)),
                       hashF(id + uint3(22, 0, 0)),
                       hashF(id + uint3(33, 0, 0))) - 0.5f;
    float d = length(f - (0.5f + sp * 0.62f));
    float m = saturate(1.0f - d * 3.6f);
    m = m * m * m;
    float tw = 0.82f + 0.18f * sin(t * 2.5f + h0 * 311.0f);
    float3 tint = mix(float3(1.0f, 0.82f, 0.64f),
                      float3(0.70f, 0.84f, 1.0f),
                      hashF(id + uint3(44, 0, 0)));
    return tint * m * gain * tw;
}

static float3 background(float3 n, float t) {
    float3 c = float3(0.003f, 0.004f, 0.009f);
    float3 bn = normalize(float3(0.37f, 1.0f, 0.28f));
    float band = exp(-dot(n, bn) * dot(n, bn) / 0.042f);
    float neb = fbm3(n * 2.6f + 5.1f);
    c += band * (0.045f + 0.14f * neb) * float3(0.72f, 0.78f, 1.0f);
    c += band * band * (0.5f + 0.5f * noise3(n * 6.0f)) * float3(0.16f, 0.07f, 0.14f);
    c += starLayer(n, 210.0f, t, 0.10f, 0.55f);
    c += starLayer(n, 72.0f, t, 0.045f, 1.7f);
    return c;
}

// ------------------------------------- Schwarzschild null geodesics
// State: position (r, th, ph), velocity d/dl (dr, dth, dph) in an affine
// parameter l. E = conserved energy (per unit affine length), Lz / Lth the
// conserved angular momentum components.
struct GS { float r, th, ph, dr, dth, dph; };
struct GOut { float vr, vt, vp, ar, at, ap; };

static inline GOut gderiv(const GS &s, float E) {
    GOut o;
    float f  = fmax(1.0f - 2.0f / s.r, 0.03f);
    float sn = fmax(sin(s.th), 1e-5f);
    float cs = cos(s.th);
    float s2 = sn * sn;
    float invf3 = 1.0f / (f * f * f);
    // r-ddot = M E^2/(f^3 r^2) - M rdot^2/(f^3 r^2) - (r/f)(thdot^2 + sin^2 th phdot^2)
    o.ar = invf3 * (E * E - s.dr * s.dr) / (s.r * s.r)
           - (s.r / f) * (s.dth * s.dth + s2 * s.dph * s.dph);
    o.at = (s.dr * s.dth) / s.r - sn * cs * s.dph * s.dph;
    o.ap = (s.dr * s.dph) / s.r + (cs / sn) * s.dth * s.dph;
    o.vr = s.dr; o.vt = s.dth; o.vp = s.dph;
    return o;
}

static inline void gstep(GS &s, float h, float E) {
    GOut a = gderiv(s, E);
    GS s2 = s;
    s2.r += 0.5f * h * a.vr; s2.th += 0.5f * h * a.vt; s2.ph += 0.5f * h * a.vp;
    s2.dr += 0.5f * h * a.ar; s2.dth += 0.5f * h * a.at; s2.dph += 0.5f * h * a.ap;
    GOut b = gderiv(s2, E);
    GS s3 = s;
    s3.r += 0.5f * h * b.vr; s3.th += 0.5f * h * b.vt; s3.ph += 0.5f * h * b.vp;
    s3.dr += 0.5f * h * b.ar; s3.dth += 0.5f * h * b.at; s3.dph += 0.5f * h * b.ap;
    GOut c = gderiv(s3, E);
    GS s4 = s;
    s4.r += h * c.vr; s4.th += h * c.vt; s4.ph += h * c.vp;
    s4.dr += h * c.ar; s4.dth += h * c.at; s4.dph += h * c.ap;
    GOut d = gderiv(s4, E);
    s.r  += (h / 6.0f) * (a.vr + 2.0f * b.vr + 2.0f * c.vr + d.vr);
    s.th += (h / 6.0f) * (a.vt + 2.0f * b.vt + 2.0f * c.vt + d.vt);
    s.ph += (h / 6.0f) * (a.vp + 2.0f * b.vp + 2.0f * c.vp + d.vp);
    s.dr += (h / 6.0f) * (a.ar + 2.0f * b.ar + 2.0f * c.ar + d.ar);
    s.dth+= (h / 6.0f) * (a.at + 2.0f * b.at + 2.0f * c.at + d.at);
    s.dph+= (h / 6.0f) * (a.ap + 2.0f * b.ap + 2.0f * c.ap + d.ap);
}

// ------------------------------------------------- accretion disk light
// Thin disk in the equatorial plane, r in [6, ~34].
//   T(r)  = 1.5e7 K * (6/r)^(3/4)      (Shakura-Sunyaev inner-peak profile)
//   g     = sqrt(1-2M/r) / (gamma (1 - v n_phi))
//           with v = sqrt(M/(r-2M)) the Keplerian speed measured by a local
//           static observer; g multiplies the temperature (Doppler shift)
//           and I ~ g^4 (relativistic beaming + redshifted blackbody).
static float3 diskEmit(GS s, float rc, float E, float Lz, float time) {
    float fc = fmax(1.0f - 2.0f / rc, 0.05f);
    // photon phi-direction in the local static orthonormal frame
    float nph = (Lz / rc) * sqrt(fc) / E;
    float v   = sqrt(1.0f / fmax(rc - 2.0f, 0.1f));
    float gam = 1.0f / sqrt(fmax(1.0f - v * v, 1e-4f));
    float g   = sqrt(fc) / (gam * fmax(1.0f - v * nph, 0.05f));

    float prof = pow(6.0f / rc, 1.5f);
    prof *= smoothstep(5.95f, 6.6f, rc);
    prof *= exp(-fmax(rc - 24.0f, 0.0f) / 4.0f);

    float spiral = 2.2f * s.ph - 0.45f * rc - 3.4f * time * pow(rc, -1.5f);
    float turb   = 0.82f + 0.18f * sin(spiral);
    float fine   = 0.94f + 0.06f * noise3(float3(rc * 0.7f, s.ph * 30.0f, 0.0f));

    // path-length boost when the ray grazes the disk plane
    float nz    = fmax(fabs(s.dr) * sqrt(fc) / E, 0.18f);
    float boost = fmin(1.0f / nz, 3.5f);

    float I  = 5.0f * prof * turb * fine * boost * pow(g, 4.0f);
    float Tv = clamp(1.5e7f * pow(6.0f / rc, 0.75f) * 1.05e-3f * g, 900.0f, 40000.0f);
    return blackbody(Tv) * I;
}

// ------------------------------------------------------------------- ray trace
kernel void raytrace(texture2d<half, access::write> dst [[texture(0)]],
                     constant URT &U [[buffer(0)]],
                     uint2 gid [[thread_position_in_grid]]) {
    uint W = dst.get_width();
    uint H = dst.get_height();
    if (gid.x >= W || gid.y >= H) return;

    float2 px = (float2(gid) + 0.5f) / float2(W, H);
    float2 nd = px * 2.0f - 1.0f;
    nd.y = -nd.y;

    float3 cp  = float3(U.cam_x, U.cam_y, U.cam_z);
    float3 fwd = normalize(float3(U.fwd_x, U.fwd_y, U.fwd_z));
    float3 rgt = normalize(float3(U.rgt_x, U.rgt_y, U.rgt_z));
    float3 upv = cross(rgt, fwd);
    float3 dir = normalize(fwd + U.tan_fov * (nd.x * U.aspect * rgt + nd.y * upv));

    // camera in spherical coords
    float rC  = U.cam_r;
    float3 rh = cp / rC;
    float thC = acos(clamp(rh.z, -1.0f, 1.0f));
    float phC = atan2(rh.y, rh.x);
    float3 phat = cross(float3(0.0f, 0.0f, 1.0f), rh);
    phat = (length(phat) < 0.5f) ? float3(1.0f, 0.0f, 0.0f) : normalize(phat);
    float3 that = cross(phat, rh);

    // orthonormal ray direction at the camera
    float nr  = dot(dir, rh);
    float ntt = dot(dir, that);
    float np  = dot(dir, phat);
    float fC   = fmax(1.0f - 2.0f / rC, 0.5f);
    float E    = sqrt(fC);
    float Lth  = rC * ntt;
    float Lz   = rC * sin(thC) * np;
    float sinThC = fmax(sin(thC), 1e-4f);

    GS s;
    s.r  = rC;  s.th = thC;  s.ph = phC;
    s.dr = nr * E; s.dth = ntt / rC; s.dph = np / (rC * sinThC);

    float3 col = float3(0.0f, 0.0f, 0.0f);
    bool done = false;
    float rPrev = s.r;
    for (int i = 0; i < 1000 && !done; ++i) {
        float Ltot = sqrt(Lth * Lth + Lz * Lz);
        float h = clamp(0.022f * s.r * s.r / fmax(Ltot, 0.6f), 0.008f, 0.4f);
        float sPrev = s.th - 1.5707963f;
        rPrev = s.r;
        gstep(s, h, E);
        float sNow = s.th - 1.5707963f;

        if (sPrev * sNow < 0.0f) {
            float u  = sPrev / (sPrev - sNow);
            float rc = rPrev + u * (s.r - rPrev);
            if (rc > 5.95f && rc < 34.0f) {
                GS sc = s;
                sc.r = rc;
                sc.th = 1.5707963f;
                col = diskEmit(sc, rc, E, Lz, U.time);
                done = true;
                break;
            }
        }
        if (s.r <= 2.0f) { done = true; break; }            // captured: black
        if (s.r > 85.0f && s.dr > 0.0f) {
            float3 n = normalize(float3(s.dr, s.r * s.dth,
                                        s.r * sin(s.th) * s.dph));
            col = background(n, U.time);                    // escaped: lensed sky
            done = true;
            break;
        }
    }
    if (!done) col = float3(1.7f, 2.0f, 2.6f);              // photon ring
    dst.write(half4(half3(col), 0.0h), gid);
}

// ---------------------------------------------------------------- downsample
kernel void downsample(texture2d<half, access::read> src [[texture(0)]],
                       texture2d<half, access::write> dst [[texture(1)]],
                       uint2 gid [[thread_position_in_grid]]) {
    uint W = dst.get_width(), H = dst.get_height();
    if (gid.x >= W || gid.y >= H) return;
    half3 a = src.read(2 * gid.x,       2 * gid.y       ).rgb
            + src.read(2 * gid.x + 1u,  2 * gid.y       ).rgb
            + src.read(2 * gid.x,       2 * gid.y + 1u  ).rgb
            + src.read(2 * gid.x + 1u,  2 * gid.y + 1u  ).rgb;
    dst.write(half4(a * 0.25h, 0.0h), gid);
}

// --------------------------------------------------- separable Gaussian blur
constant float BW[7] = {0.0185f, 0.0342f, 0.0563f, 0.0831f,
                        0.1097f, 0.1296f, 0.1370f};

kernel void blurH(texture2d<half, access::read> src [[texture(0)]],
                  texture2d<half, access::write> dst [[texture(1)]],
                  uint2 gid [[thread_position_in_grid]]) {
    uint W = dst.get_width(), H = dst.get_height();
    if (gid.x >= W || gid.y >= H) return;
    half3 a = half3(0.0h, 0.0h, 0.0h);
    for (int i = 0; i < 7; ++i) {
        int x = int(gid.x) - 6 + i;
        if (x < 0) x = 0;
        else if (x >= int(W)) x = int(W) - 1;
        a += half3(BW[i]) * src.read(uint2(x, gid.y)).rgb;
    }
    dst.write(half4(a, 0.0h), gid);
}

kernel void blurV(texture2d<half, access::read> src [[texture(0)]],
                  texture2d<half, access::write> dst [[texture(1)]],
                  uint2 gid [[thread_position_in_grid]]) {
    uint W = dst.get_width(), H = dst.get_height();
    if (gid.x >= W || gid.y >= H) return;
    half3 a = half3(0.0h, 0.0h, 0.0h);
    for (int i = 0; i < 7; ++i) {
        int y = int(gid.y) - 6 + i;
        if (y < 0) y = 0;
        else if (y >= int(H)) y = int(H) - 1;
        a += half3(BW[i]) * src.read(uint2(gid.x, y)).rgb;
    }
    dst.write(half4(a, 0.0h), gid);
}

// ------------------------------------------------------------------- composite
struct FSIn { float4 pos [[position]]; float2 uv; };

vertex FSIn fsq(uint id [[vertex_id]]) {
    float2 p;
    p.x = (id == 1) ? 3.0f : -1.0f;
    p.y = (id == 2) ? 3.0f : -1.0f;
    FSIn o;
    o.pos = float4(p, 0.0f, 1.0f);
    o.uv  = p * 0.5f + 0.5f;
    return o;
}

fragment half4 composite(FSIn in [[stage_in]],
                         texture2d<half> rt [[texture(0)]],
                         texture2d<half> bl [[texture(1)]],
                         sampler smp [[sampler(0)]]) {
    float2 uv = in.uv;
    half3 c = rt.sample(smp, uv).rgb + bl.sample(smp, uv).rgb * 0.5h;
    half d = length(uv - 0.5h);
    c *= 1.0h - 0.30h * saturate(d * 1.45h - 0.42h);
    c = half3(aces(float3(c)));
    c = pow(c, half3(1.0h / 2.2h));
    return half4(c, 1.0h);
}

// -------------------------------------------------------------- spacetime grid
// Flamm paraboloid embedding of the equatorial Schwarzschild spatial slice:
//   z(r) = 2 sqrt(2 M (r - 2M))      ("the trapdoor")
// positions precomputed on the CPU; r and phi passed as vertex attributes.
struct V5 { float px, py, pz, r, ph; };
struct FVIn { float4 pos [[position]]; float r; float ph; };

vertex FVIn funnelVS(uint vid [[vertex_id]],
                     constant V5 *vtx [[buffer(0)]],
                     constant UMesh &U [[buffer(1)]]) {
    FVIn o;
    o.pos = U.proj * U.view * float4(vtx[vid].px, vtx[vid].py, vtx[vid].pz, 1.0f);
    o.r  = vtx[vid].r;
    o.ph = vtx[vid].ph;
    return o;
}

static float gridline(float x) {
    float d = abs(fract(x) - 0.5f);
    return 1.0f - smoothstep(0.42f, 0.5f, d);
}

fragment half4 funnelFS(FVIn in [[stage_in]],
                        constant UMesh &U [[buffer(0)]]) {
    float r = in.r;
    float nearThroat = exp(-(r - 2.0f) / 16.0f);
    float gPhi = gridline(in.ph * 10.1859f);      // 64 radial spokes
    float gR   = gridline((r - 2.0f) / 1.5f);     // circular rings
    float pulse = 0.12f * sin(0.55f * r - 2.6f * U.time) * exp(-r / 45.0f);
    float bright = 0.28f + 0.85f * nearThroat + pulse;
    float3 col = float3(0.012f, 0.028f, 0.050f)
        + float3(0.22f, 0.70f, 1.0f) * (gPhi * 0.5f + gR * 0.7f) * bright
        + float3(1.0f, 0.30f, 0.15f) * (1.0f - smoothstep(0.05f, 0.6f, r - 2.0f)) * 0.5f
        + float3(1.0f, 1.0f, 1.0f)  * (1.0f - smoothstep(0.0f, 0.22f, abs(r - 3.0f))) * 0.7f
        + float3(1.0f, 0.70f, 0.25f)* (1.0f - smoothstep(0.0f, 0.20f, abs(r - 6.0f))) * 0.55f
        + float3(0.5f, 0.80f, 1.0f) * (1.0f - smoothstep(0.0f, 0.30f, abs(r - 30.0f))) * 0.2f;
    // accretion disk glow lying on the embedding surface, r in [6, 30]
    float dp = smoothstep(5.95f, 6.8f, r) * exp(-fmax(r - 24.0f, 0.0f) / 5.0f);
    if (dp > 0.001f) {
        float arm = 0.75f + 0.25f * sin(2.2f * in.ph - 0.45f * r
                                        - 3.4f * U.time * pow(r, -1.5f));
        col += float3(1.0f, 0.62f, 0.28f) * dp * arm * (0.5f + 0.6f * nearThroat);
    }
    float a = 0.34f + 0.5f * nearThroat + 0.18f * (gPhi + gR);
    return half4(half3(col), half(a));
}

fragment half4 capFS(FVIn in [[stage_in]]) {
    float r = in.r;
    float3 col = float3(0.010f, 0.012f, 0.020f);
    col += float3(1.0f, 0.3f, 0.12f) * (1.0f - smoothstep(0.05f, 0.55f, r - 1.9f)) * 0.55f;
    return half4(half3(col), half(0.92f));
}

// --------------------------------------------------------- photon geodesics
struct LV { float px, py, pz, cx, cy, cz, a; };
struct LIn { float4 pos [[position]]; float3 c; float a; };

vertex LIn lineVS(uint vid [[vertex_id]],
                  constant LV *vtx [[buffer(0)]],
                  constant UMesh &U [[buffer(1)]]) {
    LIn o;
    o.pos = U.proj * U.view * float4(vtx[vid].px, vtx[vid].py, vtx[vid].pz, 1.0f);
    o.c = float3(vtx[vid].cx, vtx[vid].cy, vtx[vid].cz);
    o.a = vtx[vid].a;
    return o;
}

fragment half4 lineFS(LIn in [[stage_in]]) {
    return half4(half3(in.c) * in.a, in.a);
}

// ------------------------------------------------------------------------ HUD
struct HIn { float4 pos [[position]]; float2 uv; };

vertex HIn hudVS(uint id [[vertex_id]], constant float4 &q [[buffer(0)]]) {
    float2 v = (id == 0) ? float2(0.0f, 0.0f)
             : (id == 1) ? float2(1.0f, 0.0f)
             : (id == 2) ? float2(1.0f, 1.0f)
                         : float2(0.0f, 1.0f);
    HIn o;
    o.pos = float4(2.0f * (q.x + v.x * q.z) - 1.0f,
                   1.0f - 2.0f * (q.y + v.y * q.w),
                   0.0f, 1.0f);
    o.uv = float2(v.x, 1.0f - v.y);  // texture row 0 is the top of the text
    return o;
}

fragment half4 hudFS(HIn in [[stage_in]],
                     texture2d<float> tex [[texture(0)]],
                     sampler smp [[sampler(0)]]) {
    return half4(tex.sample(smp, in.uv).rgba);
}
)metal";

} // namespace shaders
