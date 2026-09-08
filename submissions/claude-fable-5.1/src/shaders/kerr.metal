// kerr.metal — GPU side of the Kerr black hole renderer.
//   traceKernel : backward null-geodesic ray tracing → G-buffer (per jittered sample layer)
//   shadeKernel : physically based shading of disk / lensed grid / sky from the G-buffer
//   bloom*      : cheap separable bloom (camera glare, cosmetic, toggleable)
//   blit*       : tone-map to the drawable and write the ray-traced depth
//   grid*       : rasterised embedding-diagram ("rubber sheet") mesh, depth-tested
#include <metal_stdlib>
using namespace metal;
#include "shader_types.h"
#include "kerr_geodesic.h"

// ---------------------------------------------------------------- utilities
static inline float hash13(float3 p) {
    p = fract(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return fract((p.x + p.y) * p.z);
}
static inline float hash31(float n) { return fract(sin(n) * 43758.5453123f); }
static inline float3 hash33(float3 p) {
    p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)), dot(p, float3(269.5f, 183.3f, 246.1f)), dot(p, float3(113.5f, 271.9f, 124.6f)));
    return fract(sin(p) * 43758.5453123f);
}
// Value noise on an integer lattice, smooth (quintic) interpolation.
static inline float vnoise(float3 p) {
    float3 i = floor(p), f = fract(p);
    f = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float n000 = hash13(i + float3(0, 0, 0)), n100 = hash13(i + float3(1, 0, 0));
    float n010 = hash13(i + float3(0, 1, 0)), n110 = hash13(i + float3(1, 1, 0));
    float n001 = hash13(i + float3(0, 0, 1)), n101 = hash13(i + float3(1, 0, 1));
    float n011 = hash13(i + float3(0, 1, 1)), n111 = hash13(i + float3(1, 1, 1));
    float x00 = mix(n000, n100, f.x), x10 = mix(n010, n110, f.x);
    float x01 = mix(n001, n101, f.x), x11 = mix(n011, n111, f.x);
    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}
static inline float fbm(float3 p, int oct) {
    float s = 0.0f, a = 0.5f, norm = 0.0f;
    for (int i = 0; i < oct; ++i) { s += a * vnoise(p); norm += a; p = p * 2.03f + float3(17.1f, 9.7f, 3.3f); a *= 0.5f; }
    return s / norm;
}

// Blackbody radiance (linear sRGB, relative) from the CPU-tabulated LUT, log10 T axis.
static inline float3 blackbody(device const float4 *lut, uint n, float logTmin, float logTmax, float T) {
    float u = (log10(max(T, 1.0f)) - logTmin) / (logTmax - logTmin) * float(n - 1);
    u = clamp(u, 0.0f, float(n - 1));
    uint i0 = uint(u); uint i1 = min(i0 + 1, n - 1); float f = u - float(i0);
    return mix(lut[i0].xyz, lut[i1].xyz, f);
}

// ---------------------------------------------------------------- trace
kernel void traceKernel(texture2d_array<float, access::write> gA [[texture(0)]],
                        texture2d_array<float, access::write> gB [[texture(1)]],
                        constant TraceUniforms &U [[buffer(0)]],
                        uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= U.width || gid.y >= U.height) return;
    const float px = (float(gid.x) + U.jitterX) / float(U.width) * 2.0f - 1.0f;
    const float py = 1.0f - (float(gid.y) + U.jitterY) / float(U.height) * 2.0f;
    // Viewing direction in the ZAMO frame basis (r̂, θ̂, φ̂): forward = -r̂, up = -θ̂, right = +φ̂.
    float3 n = normalize(float3(-1.0f, -py * U.tanHalfFov, px * U.tanHalfFov * U.aspect));
    // Optional free-look: yaw about the local vertical (-θ̂), pitch about the local right (φ̂).
    if (U.yaw != 0.0f || U.pitch != 0.0f) {
        const float cy = cos(U.yaw), sy = sin(U.yaw);
        float3 r1 = float3(n.x * cy - n.z * sy, n.y, n.x * sy + n.z * cy);
        const float cp = cos(U.pitch), sp = sin(U.pitch);
        n = float3(r1.x * cp - r1.y * sp, r1.x * sp + r1.y * cp, r1.z);
    }
    KGState<float> y; KGConst<float> c;
    kg_camera_ray<float>(U.camR, U.camTh, U.camPh, U.a, n.x, n.y, n.z, y, c);

    KGParams<float> p;
    p.rh = U.rh; p.rStop = U.rStop; p.rDoom = U.rDoom; p.rEsc = U.rEsc;
    p.rIsco = U.rIsco; p.rDiskOut = U.rDiskOut; p.rGridOut = U.rGridOut;
    p.eps = U.eps; p.epsAng = U.epsAng;
    p.diskOn = int(U.diskOn); p.gridOn = int(U.gridOn); p.maxSteps = int(U.maxSteps);

    const KGHit<float> h = kg_trace<float>(y, c, p, U.camX, U.camY, U.camZ);
    float4 A, B;
    if (h.type == KG_HIT_SKY) {
        A = float4(0.0f, h.dx, h.dy, h.dz);
        B = float4(h.depth, h.dt, 1.0f / c.E, float(h.steps));      // 1/E: blueshift of light from infinity at the camera
    } else if (h.type == KG_HIT_DISK || h.type == KG_HIT_GRID) {
        A = float4(float(h.type), h.r, h.ph, h.g);
        B = float4(h.depth, h.dt, h.mu, float(h.steps));
    } else {
        A = float4(2.0f, 0.0f, 0.0f, 0.0f);
        B = float4(h.depth, 0.0f, 0.0f, float(h.steps));
    }
    gA.write(A, gid, U.layer);
    gB.write(B, gid, U.layer);
}

// ---------------------------------------------------------------- shading
struct ShadeCtx {
    constant ShadeUniforms &U;
    device const float4 *bbLut;
    device const float *diskLut;
};

static inline float3 shadeSky(float3 d, float gsky, thread const ShadeCtx &C) {
    // --- stars: one candidate star per cell of a cube-map lattice
    const float3 ad = abs(d);
    int face; float2 uv; float ma;
    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0 ? 0 : 1; ma = ad.x; uv = float2(d.y, d.z) / ma; }
    else if (ad.y >= ad.z)            { face = d.y > 0 ? 2 : 3; ma = ad.y; uv = float2(d.x, d.z) / ma; }
    else                              { face = d.z > 0 ? 4 : 5; ma = ad.z; uv = float2(d.x, d.y) / ma; }
    const float N = 180.0f;
    const float2 cell = floor((uv * 0.5f + 0.5f) * N);
    float3 col = float3(0.0f);
    const float sigma = C.U.pixelAngle * 0.8f;
    for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        const float2 cc = cell + float2(i, j);
        const float3 hsh = hash33(float3(cc, float(face) * 7.13f + 1.0f));
        if (hsh.x > 0.42f) continue;                       // cell has no star
        const float2 suv = ((cc + hsh.yz) / N) * 2.0f - 1.0f;
        float3 sd;
        switch (face) {
            case 0: sd = float3( 1.0f, suv.x, suv.y); break;
            case 1: sd = float3(-1.0f, suv.x, suv.y); break;
            case 2: sd = float3(suv.x,  1.0f, suv.y); break;
            case 3: sd = float3(suv.x, -1.0f, suv.y); break;
            case 4: sd = float3(suv.x, suv.y,  1.0f); break;
            default: sd = float3(suv.x, suv.y, -1.0f); break;
        }
        sd = normalize(sd);
        const float cosang = dot(sd, d);
        const float ang = sqrt(max(0.0f, 2.0f - 2.0f * cosang));   // small-angle chord ≈ angle
        if (ang > 4.0f * sigma) continue;
        const float3 h2 = hash33(float3(cc * 3.1f, float(face) * 2.7f + 11.0f));
        const float bright = pow(h2.x, 6.0f) * 3.0f + 0.02f;          // steep luminosity function
        const float T = 2600.0f * exp(h2.y * 2.6f);                    // 2600 K .. 35000 K
        const float3 bb = blackbody(C.bbLut, C.U.lutN, C.U.logTmin, C.U.logTmax, T * gsky);
        const float lum = max(bb.y, 1e-4f);
        col += bb / lum * bright * exp(-(ang * ang) / (2.0f * sigma * sigma)) * 0.9f;
    }
    // --- diffuse galactic band (procedural), surface brightness scaled by g⁴ (bolometric invariant I/ν³)
    const float3 gn = normalize(float3(0.25f, 0.55f, 0.8f));
    const float lat = asin(clamp(dot(d, gn), -1.0f, 1.0f));
    const float band = exp(-lat * lat / (2.0f * 0.16f * 0.16f));
    const float cloud = fbm(d * 5.0f, 5);
    const float dust = fbm(d * 11.0f + 3.0f, 4);
    const float mw = band * (0.25f + 1.5f * cloud * cloud) * (1.0f - 0.7f * smoothstep(0.45f, 0.7f, dust) * band);
    const float3 mwCol = blackbody(C.bbLut, C.U.lutN, C.U.logTmin, C.U.logTmax, 5200.0f * gsky);
    col += mwCol / max(mwCol.y, 1e-4f) * mw * 0.035f * pow(gsky, 4.0f);
    col += float3(0.0025f, 0.003f, 0.0045f) * pow(gsky, 4.0f);   // faint airglow-like floor so the shadow reads black
    return col * C.U.skyBrightness;
}

static inline float3 shadeDisk(float r, float ph, float g, float dt, float mu, thread const ShadeCtx &C) {
    constant ShadeUniforms &U = C.U;
    // Novikov–Thorne temperature profile (tabulated on the CPU in double precision)
    const float u = clamp((r - U.rIsco) / (U.rDiskOut - U.rIsco), 0.0f, 1.0f) * float(U.diskLutN - 1);
    const uint i0 = uint(u); const uint i1 = min(i0 + 1, U.diskLutN - 1);
    const float Tn = mix(C.diskLut[i0], C.diskLut[i1], u - float(i0));
    // Keplerian co-rotating azimuth at the retarded emission time (time-of-flight corrected)
    const float Om = kg_omega_kepler<float>(r, U.a);
    const float tEm = U.time - dt;
    const float psi = ph - Om * tEm;
    // Turbulent brightness pattern frozen into the fluid: periodic in ψ, elongated azimuthally by shear,
    // slowly evolving on the local dynamical time 1/Ω.
    const float lr = log(r);
    const float3 q = float3(cos(psi) * 2.2f, sin(psi) * 2.2f, lr * 9.0f);
    const float evo = tEm * Om * 0.12f;
    float n1 = fbm(q + float3(0.0f, 0.0f, evo), 4);
    float n2 = fbm(q * 2.7f + float3(evo * 1.7f, -evo, 0.0f), 3);
    const float pattern = 1.0f + U.turbulence * ((n1 - 0.5f) * 1.6f + (n2 - 0.5f) * 0.7f);
    // Soft outer truncation (the physical NT disk continues; we cut it for the visualisation)
    const float edge = 1.0f - smoothstep(0.86f, 1.0f, (r - U.rIsco) / (U.rDiskOut - U.rIsco));
    const float Teff = U.tMax * Tn * max(pattern, 0.05f);
    // The observed spectrum of a redshifted blackbody is exactly a blackbody at g·T
    float3 col = blackbody(C.bbLut, U.lutN, U.logTmin, U.logTmax, g * Teff);
    // Grey-atmosphere limb darkening I(μ) ∝ (1 + 2.06 μ), normalised to conserve flux
    const float limb = mix(1.0f, (1.0f + 2.06f * mu) / 2.373f, U.limbDarkening);
    return col * limb * edge;
}

static inline float3 shadeGrid(float r, float ph, float g, float depth, thread const ShadeCtx &C) {
    constant ShadeUniforms &U = C.U;
    const float2 xy = r * float2(cos(ph), sin(ph));
    const float w = max(0.035f, depth * U.pixelAngle * 0.9f);          // ~1 px line half-width in world units
    const float2 dd = abs(fract(xy / U.gridSpacing + 0.5f) - 0.5f) * U.gridSpacing;
    float line = 1.0f - smoothstep(0.0f, w, min(dd.x, dd.y));
    // concentric circles every 5 spacing units and a bright ring just outside the horizon
    const float rc = abs(fract(r / (U.gridSpacing * 2.5f) + 0.5f) - 0.5f) * U.gridSpacing * 2.5f;
    line = max(line, 0.6f * (1.0f - smoothstep(0.0f, w, rc)));
    const float ring = 1.0f - smoothstep(0.0f, 3.0f * w, abs(r - U.rh * 1.02f));
    const float3 bbc = blackbody(C.bbLut, U.lutN, U.logTmin, U.logTmax, 6500.0f * g);
    const float3 tint = float3(0.55f, 0.85f, 1.0f);
    float3 col = bbc * tint * (line * 0.9f + 0.03f) + float3(1.0f, 0.55f, 0.25f) * ring * pow(g, 4.0f) * 1.5f;
    return col * 0.6f;
}

kernel void shadeKernel(texture2d_array<float, access::read> gA [[texture(0)]],
                        texture2d_array<float, access::read> gB [[texture(1)]],
                        texture2d<float, access::write> outHDR [[texture(2)]],
                        constant ShadeUniforms &U [[buffer(0)]],
                        device const float4 *bbLut [[buffer(1)]],
                        device const float *diskLut [[buffer(2)]],
                        uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= U.width || gid.y >= U.height) return;
    ShadeCtx C{U, bbLut, diskLut};
    float3 sum = float3(0.0f);
    float depth0 = 1e9f;
    for (uint l = 0; l < U.layers; ++l) {
        const float4 A = gA.read(gid, l);
        const float4 B = gB.read(gid, l);
        const int type = int(A.x + 0.5f);
        float3 col = float3(0.0f);
        if (type == KG_HIT_SKY)        col = shadeSky(A.yzw, B.z, C);
        else if (type == KG_HIT_DISK)  col = shadeDisk(A.y, A.z, A.w, B.y, B.z, C);
        else if (type == KG_HIT_GRID)  col = shadeGrid(A.y, A.z, A.w, B.x, C);
        if (l == 0) depth0 = B.x;
        col = select(col, float3(0.0f), isnan(col) || isinf(col));   // never let a bad sample poison the bloom
        sum += col;
    }
    outHDR.write(float4(sum / float(max(U.layers, 1u)), depth0), gid);
}

// ---------------------------------------------------------------- bloom
kernel void bloomDownKernel(texture2d<float, access::sample> src [[texture(0)]],
                            texture2d<float, access::write> dst [[texture(1)]],
                            constant BloomUniforms &U [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= U.dstW || gid.y >= U.dstH) return;
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    const float2 uv = (float2(gid) + 0.5f) / float2(U.dstW, U.dstH);
    const float2 px = 1.0f / float2(U.srcW, U.srcH);
    float3 c = float3(0.0f);
    c += src.sample(s, uv + px * float2(-1, -1)).xyz; c += src.sample(s, uv + px * float2(1, -1)).xyz;
    c += src.sample(s, uv + px * float2(-1,  1)).xyz; c += src.sample(s, uv + px * float2(1,  1)).xyz;
    c *= 0.25f;
    const float lum = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    const float soft = clamp(lum - U.threshold + U.knee, 0.0f, 2.0f * U.knee);
    const float contrib = max(lum - U.threshold, soft * soft / (4.0f * U.knee + 1e-4f)) / max(lum, 1e-4f);
    dst.write(float4(c * contrib, 1.0f), gid);
}

kernel void bloomBlurKernel(texture2d<float, access::sample> src [[texture(0)]],
                            texture2d<float, access::write> dst [[texture(1)]],
                            constant BloomUniforms &U [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= U.dstW || gid.y >= U.dstH) return;
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    const float2 uv = (float2(gid) + 0.5f) / float2(U.dstW, U.dstH);
    const float2 step = float2(U.dirX, U.dirY) / float2(U.srcW, U.srcH);
    const float w[9] = {0.1974f, 0.1747f, 0.1210f, 0.0656f, 0.0278f, 0.0092f, 0.0024f, 0.0005f, 0.0001f};
    float3 c = src.sample(s, uv).xyz * w[0];
    for (int i = 1; i < 9; ++i) {
        c += src.sample(s, uv + step * float(i) * 1.5f).xyz * w[i];
        c += src.sample(s, uv - step * float(i) * 1.5f).xyz * w[i];
    }
    dst.write(float4(c, 1.0f), gid);
}

// ---------------------------------------------------------------- blit
struct BlitOut { float4 color [[color(0)]]; float depth [[depth(any)]]; };
struct FSVert { float4 pos [[position]]; float2 uv; };

vertex FSVert blitVertex(uint vid [[vertex_id]]) {
    FSVert o;
    const float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
    o.pos = float4(p, 0.0f, 1.0f);
    o.uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}

static inline float3 acesTonemap(float3 x) {
    return clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
}

fragment BlitOut blitFragment(FSVert in [[stage_in]],
                              texture2d<float, access::sample> hdr [[texture(0)]],
                              texture2d<float, access::sample> bloom [[texture(1)]],
                              constant BlitUniforms &U [[buffer(0)]])
{
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    const float4 h = hdr.sample(s, in.uv);
    float3 c = h.xyz;
    if (U.flags & 1u) c += bloom.sample(s, in.uv).xyz * U.bloomStrength;
    c = acesTonemap(c * U.exposure);
    BlitOut o;
    o.color = float4(c, 1.0f);
    o.depth = clamp(h.w / U.depthFar, 0.0f, 1.0f);
    return o;
}

// ---------------------------------------------------------------- embedding grid mesh
struct GridVert { float4 pos [[position]]; float3 world; };
struct GridOut { float4 color [[color(0)]]; float depth [[depth(any)]]; };

vertex GridVert gridVertex(device const packed_float3 *verts [[buffer(0)]],
                           constant GridUniforms &U [[buffer(1)]],
                           uint vid [[vertex_id]])
{
    GridVert o;
    const float3 w = float3(verts[vid]);
    o.world = w;
    o.pos = U.viewProj * float4(w, 1.0f);
    return o;
}

fragment GridOut gridFragment(GridVert in [[stage_in]], constant GridUniforms &U [[buffer(1)]]) {
    const float2 xy = in.world.xy;
    const float2 q = xy / U.gridSpacing;
    const float2 fw = max(fwidth(q), 1e-5f);
    const float2 gl = abs(fract(q - 0.5f) - 0.5f) / (fw * 1.3f);
    float line = 1.0f - min(min(gl.x, gl.y), 1.0f);
    // depth-of-well colour: cool at the flat rim, hot in the throat
    const float wellT = clamp(in.world.z / U.zMin, 0.0f, 1.0f);
    const float3 cCool = float3(0.35f, 0.75f, 1.0f), cHot = float3(1.0f, 0.45f, 0.15f);
    const float3 lc = mix(cCool, cHot, smoothstep(0.15f, 0.95f, wellT));
    const float dist = length(in.world - U.camPos);
    const float fade = exp(-dist / 90.0f) * (1.0f - smoothstep(0.85f, 1.0f, length(xy) / U.rOut));
    const float aLine = line * U.alpha * fade;
    const float aFill = 0.05f * U.alpha * fade * (0.5f + 0.5f * wellT);
    GridOut o;
    const float a = aLine + aFill * (1.0f - aLine);
    o.color = float4(lc * (aLine + 0.4f * aFill * (1.0f - aLine)), a);   // premultiplied
    o.depth = clamp(dist / U.depthFar, 0.0f, 1.0f);
    return o;
}
