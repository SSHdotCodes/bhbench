// renderer.mm -- Metal implementation.  Pipeline per frame:
//   1. raytrace   (compute)  one Kerr geodesic per pixel -> HDR radiance
//   2. taa_resolve(compute)  jittered progressive accumulation
//   3. bloom_down/up (compute) glow around the disk and stars
//   4. funnel pass (render, 4x MSAA) embedding diagram of spacetime with test particles
//   5. present    (render)   upscale + tone-map + funnel inset + HUD
#import "renderer.h"

#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#include <simd/simd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "kerr_shared.h"
#include "physics.hpp"
#include "shader_source.h"   // generated: kShaderSource
#include "shared_types.h"

static_assert(sizeof(RTUniforms) == 128, "RTUniforms layout");
static_assert(sizeof(PostUniforms) == 80, "PostUniforms layout");
static_assert(sizeof(FunnelVertex) == 32, "FunnelVertex layout");
static_assert(sizeof(ParticleVertex) == 32, "ParticleVertex layout");

namespace {

constexpr int kBBSize = 1024;
constexpr int kDiskSize = 1024;
constexpr double kFunnelRMax = 34.0;
constexpr int kFunnelRings = 200;
constexpr int kFunnelSegs = 168;
constexpr int kTubeRings = 30;
constexpr double kTubeStep = 0.5;
constexpr int kBloomLevels = 6;

using simd::float3;
using simd::float4x4;

float4x4 look_at(float3 eye, float3 c, float3 up)
{
    float3 f = simd::normalize(c - eye);
    float3 s = simd::normalize(simd::cross(f, up));
    float3 u = simd::cross(s, f);
    float4x4 m;
    m.columns[0] = simd::float4{s.x, u.x, -f.x, 0};
    m.columns[1] = simd::float4{s.y, u.y, -f.y, 0};
    m.columns[2] = simd::float4{s.z, u.z, -f.z, 0};
    m.columns[3] = simd::float4{-simd::dot(s, eye), -simd::dot(u, eye), simd::dot(f, eye), 1};
    return m;
}

float4x4 perspective(float fovy, float aspect, float n, float f)
{
    float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect, zs = f / (n - f);
    float4x4 m;
    m.columns[0] = simd::float4{xs, 0, 0, 0};
    m.columns[1] = simd::float4{0, ys, 0, 0};
    m.columns[2] = simd::float4{0, 0, zs, -1};
    m.columns[3] = simd::float4{0, 0, zs * n, 0};
    return m;
}

double halton(int i, int base)
{
    double f = 1.0, r = 0.0;
    while (i > 0) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}

uint64_t hash_mix(uint64_t h, double v)
{
    uint64_t x;
    std::memcpy(&x, &v, sizeof x);
    h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

// A test particle drawn on the funnel.
struct Orbiter {
    phys::Track track;
    bool analytic_circle = false;   // pure circular orbit (exactly periodic)
    double r = 0, omega = 0, phi0 = 0;
    float color[3] = {1, 1, 1};
    float size = 0.5f;
    double trail_len = 30;          // in coordinate time
    int trail_n = 60;
    double hold = 0;                // pause after a plunge before respawning
    bool plunge = false;
    const char* label = "";
};

}  // namespace

struct Renderer::Impl {
    id<MTLDevice> dev = nil;
    id<MTLLibrary> lib = nil;
    id<MTLComputePipelineState> psRaytrace = nil, psTaa = nil, psBloomDown = nil, psBloomUp = nil, psProbe = nil;
    id<MTLRenderPipelineState> psPresent = nil, psFunnel = nil, psParticle = nil;
    id<MTLDepthStencilState> dsMesh = nil, dsParticle = nil;
    id<MTLBuffer> bbBuf = nil, diskBuf = nil;

    // internal-resolution targets
    int iw = 0, ih = 0;
    id<MTLTexture> cur = nil, hist[2] = {nil, nil};
    int hist_idx = 0;
    std::vector<id<MTLTexture>> down, up;
    id<MTLTexture> black1 = nil;

    // funnel targets
    int fw = 0, fh = 0;
    id<MTLTexture> funnelTex = nil, funnelMsaa = nil, funnelDepth = nil;

    // funnel geometry
    id<MTLBuffer> fVerts = nil, fIdx = nil;
    int fIdxCount = 0;
    phys::Embedding emb;
    double funnel_spin = -1.0;
    double z_min = 0.0, z_top = 0.0;
    std::vector<Orbiter> orbiters;
    std::vector<std::vector<simd::float3>> photon_paths;   // light rays in the equatorial plane, already in funnel world space

    // HUD
    id<MTLTexture> hudTex = nil;
    double hud_built_at = -1.0;
    int hud_w = 0, hud_h = 0;
    std::string hud_key, hud_status_key, hud_slow_key;

    // tables
    double tab_spin = -1.0, tab_rout = -1.0;

    // temporal state
    uint64_t last_key = 0;
    int static_frames = 0;
    uint64_t frame_index = 0;

    // inset rect in drawable pixels (x0, y0, x1, y1), top-left origin
    float inset[4] = {0, 0, 0, 0};

    // stats
    std::atomic<double> gpu_ms{0.0};
    double fps_ema = 60.0;
    int scale_cooldown = 0;
    FrameInfo finfo;

    bool build_pipelines(std::string* err);
    void ensure_targets(int w, int h);
    void ensure_funnel_targets(int w, int h);
    void rebuild_tables(double a, double rout);
    void rebuild_funnel(double a);
    void embed_lookup(double r, double* rho, double* z) const;
    void embed_pos(double r, double phi, double lift, float out[3], double zscale) const;
    void paint_hud(const SimState& s, int dw, int dh, double wall);
};

// =============================================================================================
// Pipelines
// =============================================================================================
bool Renderer::Impl::build_pipelines(std::string* err)
{
    NSError* e = nil;
    MTLCompileOptions* opts = [MTLCompileOptions new];
    opts.languageVersion = MTLLanguageVersion3_1;
    if (@available(macOS 15.0, *)) opts.mathMode = getenv("BH_SAFE_MATH") ? MTLMathModeSafe : MTLMathModeRelaxed;
    NSString* src = [NSString stringWithUTF8String:kShaderSource];
    lib = [dev newLibraryWithSource:src options:opts error:&e];
    if (!lib) {
        if (err) *err = std::string("Metal shader compile failed: ") + [[e localizedDescription] UTF8String];
        return false;
    }
    auto fn = [&](NSString* n) { return [lib newFunctionWithName:n]; };
    auto cps = [&](NSString* n) -> id<MTLComputePipelineState> {
        NSError* ee = nil;
        id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:fn(n) error:&ee];
        if (!p && err) *err = std::string("pipeline ") + [n UTF8String] + ": " + [[ee localizedDescription] UTF8String];
        return p;
    };
    psRaytrace = cps(@"raytrace");
    psTaa = cps(@"taa_resolve");
    psBloomDown = cps(@"bloom_down");
    psBloomUp = cps(@"bloom_up");
    psProbe = cps(@"probe");
    if (!psRaytrace || !psTaa || !psBloomDown || !psBloomUp || !psProbe) return false;

    {
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = fn(@"present_vs");
        d.fragmentFunction = fn(@"present_fs");
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        psPresent = [dev newRenderPipelineStateWithDescriptor:d error:&e];
        if (!psPresent) { if (err) *err = std::string("present pipeline: ") + [[e localizedDescription] UTF8String]; return false; }
    }
    {
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = fn(@"funnel_vs");
        d.fragmentFunction = fn(@"funnel_fs");
        d.rasterSampleCount = 4;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        auto* ca = d.colorAttachments[0];
        ca.pixelFormat = MTLPixelFormatRGBA16Float;
        ca.blendingEnabled = YES;
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
        ca.sourceRGBBlendFactor = MTLBlendFactorOne;
        ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
        ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        psFunnel = [dev newRenderPipelineStateWithDescriptor:d error:&e];
        if (!psFunnel) { if (err) *err = std::string("funnel pipeline: ") + [[e localizedDescription] UTF8String]; return false; }

        d.vertexFunction = fn(@"particle_vs");
        d.fragmentFunction = fn(@"particle_fs");
        ca.destinationRGBBlendFactor = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        psParticle = [dev newRenderPipelineStateWithDescriptor:d error:&e];
        if (!psParticle) { if (err) *err = std::string("particle pipeline: ") + [[e localizedDescription] UTF8String]; return false; }
    }
    {
        MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
        d.depthCompareFunction = MTLCompareFunctionLessEqual;
        d.depthWriteEnabled = YES;
        dsMesh = [dev newDepthStencilStateWithDescriptor:d];
        d.depthWriteEnabled = NO;
        dsParticle = [dev newDepthStencilStateWithDescriptor:d];
    }
    // static tables: blackbody colours
    {
        auto rows = phys::blackbody_table(kBBSize, 300.0, 400000.0);
        bbBuf = [dev newBufferWithBytes:rows.data() length:rows.size() * sizeof(phys::BBRow) options:MTLResourceStorageModeShared];
    }
    {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:1 height:1 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        black1 = [dev newTextureWithDescriptor:d];
        uint16_t z[4] = {0, 0, 0, 0};
        [black1 replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:z bytesPerRow:8];
    }
    return true;
}

static id<MTLTexture> make_tex(id<MTLDevice> dev, int w, int h, MTLPixelFormat f, MTLTextureUsage u, int samples = 1)
{
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:std::max(w, 1) height:std::max(h, 1) mipmapped:NO];
    d.usage = u;
    d.storageMode = MTLStorageModePrivate;
    if (samples > 1) {
        d.textureType = MTLTextureType2DMultisample;
        d.sampleCount = samples;
    }
    return [dev newTextureWithDescriptor:d];
}

void Renderer::Impl::ensure_targets(int w, int h)
{
    if (w == iw && h == ih && cur) return;
    iw = w;
    ih = h;
    const auto rw = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    cur = make_tex(dev, w, h, MTLPixelFormatRGBA16Float, rw);
    hist[0] = make_tex(dev, w, h, MTLPixelFormatRGBA16Float, rw);
    hist[1] = make_tex(dev, w, h, MTLPixelFormatRGBA16Float, rw);
    down.clear();
    up.clear();
    int bw = w, bh = h;
    for (int i = 0; i < kBloomLevels; ++i) {
        bw = std::max(bw / 2, 1);
        bh = std::max(bh / 2, 1);
        down.push_back(make_tex(dev, bw, bh, MTLPixelFormatRGBA16Float, rw));
        up.push_back(i == kBloomLevels - 1 ? down.back() : make_tex(dev, bw, bh, MTLPixelFormatRGBA16Float, rw));
    }
    static_frames = 0;
}

void Renderer::Impl::ensure_funnel_targets(int w, int h)
{
    if (w == fw && h == fh && funnelTex) return;
    fw = w;
    fh = h;
    funnelTex = make_tex(dev, w, h, MTLPixelFormatRGBA16Float, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    funnelMsaa = make_tex(dev, w, h, MTLPixelFormatRGBA16Float, MTLTextureUsageRenderTarget, 4);
    funnelDepth = make_tex(dev, w, h, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, 4);
}

void Renderer::Impl::rebuild_tables(double a, double rout)
{
    if (a == tab_spin && rout == tab_rout && diskBuf) return;
    tab_spin = a;
    tab_rout = rout;
    auto T = phys::nt_temperature_table(a, rout, kDiskSize);
    diskBuf = [dev newBufferWithBytes:T.data() length:T.size() * sizeof(float) options:MTLResourceStorageModeShared];
}

// =============================================================================================
// Funnel geometry: sheet + tube, and the test particles that live on it
// =============================================================================================
void Renderer::Impl::embed_lookup(double r, double* rho, double* z) const
{
    const auto& R = emb.r;
    if (r <= R.front()) { *rho = emb.rho.front(); *z = emb.z.front(); return; }
    if (r >= R.back()) { *rho = emb.rho.back(); *z = emb.z.back(); return; }
    size_t i = std::upper_bound(R.begin(), R.end(), r) - R.begin();
    const double t = (r - R[i - 1]) / (R[i] - R[i - 1]);
    *rho = emb.rho[i - 1] * (1 - t) + emb.rho[i] * t;
    *z = emb.z[i - 1] * (1 - t) + emb.z[i] * t;
}

void Renderer::Impl::embed_pos(double r, double phi, double lift, float out[3], double zscale) const
{
    double rho, z;
    embed_lookup(r, &rho, &z);
    out[0] = (float)(rho * std::cos(phi));
    out[1] = (float)(rho * std::sin(phi));
    out[2] = (float)(z * zscale + lift);
}

void Renderer::Impl::rebuild_funnel(double a)
{
    if (a == funnel_spin && fVerts) return;
    funnel_spin = a;
    emb = phys::kerr_embedding(a, kFunnelRMax, 1 + 10 * (kFunnelRings - 1));
    const double rp = phys::r_horizon(a);
    z_min = emb.z.front();
    z_top = emb.z.back();

    std::vector<FunnelVertex> V;
    std::vector<uint32_t> I;
    // sheet
    for (int i = 0; i < kFunnelRings; ++i) {
        const size_t k = (size_t)i * 10;
        const double r = emb.r[k], rho = emb.rho[k], z = emb.z[k];
        for (int j = 0; j <= kFunnelSegs; ++j) {
            const double ph = 2.0 * M_PI * j / kFunnelSegs;
            FunnelVertex v;
            v.pos = simd::float4{(float)(rho * std::cos(ph)), (float)(rho * std::sin(ph)), (float)z, 0.f};
            v.attr = simd::float4{(float)(r * std::cos(ph)), (float)(r * std::sin(ph)), (float)r, -1.f};
            V.push_back(v);
        }
    }
    const int stride = kFunnelSegs + 1;
    for (int i = 0; i + 1 < kFunnelRings; ++i)
        for (int j = 0; j < kFunnelSegs; ++j) {
            uint32_t a0 = i * stride + j, a1 = a0 + 1, b0 = (i + 1) * stride + j, b1 = b0 + 1;
            I.insert(I.end(), {a0, b0, a1, a1, b0, b1});
        }
    // tube below the horizon: the throat continues downward (schematic: the interior is not part of the exterior slice)
    const int tube0 = (int)V.size();
    const double rho_h = emb.rho.front();
    for (int k = 0; k <= kTubeRings; ++k) {
        const double depth = k * kTubeStep;
        for (int j = 0; j <= kFunnelSegs; ++j) {
            const double ph = 2.0 * M_PI * j / kFunnelSegs;
            FunnelVertex v;
            v.pos = simd::float4{(float)(rho_h * std::cos(ph)), (float)(rho_h * std::sin(ph)), (float)(z_min - depth), 0.f};
            v.attr = simd::float4{(float)(rho_h * std::cos(ph)), (float)(rho_h * std::sin(ph)), (float)rp, (float)depth};
            V.push_back(v);
        }
    }
    for (int k = 0; k < kTubeRings; ++k)
        for (int j = 0; j < kFunnelSegs; ++j) {
            uint32_t a0 = tube0 + k * stride + j, a1 = a0 + 1, b0 = tube0 + (k + 1) * stride + j, b1 = b0 + 1;
            I.insert(I.end(), {a0, b0, a1, a1, b0, b1});
        }
    fVerts = [dev newBufferWithBytes:V.data() length:V.size() * sizeof(FunnelVertex) options:MTLResourceStorageModeShared];
    fIdx = [dev newBufferWithBytes:I.data() length:I.size() * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    fIdxCount = (int)I.size();

    // ---- test particles: real Kerr geodesics in coordinate time ----
    orbiters.clear();
    const double risco = phys::r_isco(a);
    const double horizon_pad = 0.0;
    (void)horizon_pad;
    {
        Orbiter o;
        o.analytic_circle = true;
        o.r = risco * 1.0005;
        o.omega = phys::circular(o.r, a).omega;
        o.phi0 = 0.0;
        o.color[0] = 0.35f; o.color[1] = 1.0f; o.color[2] = 0.55f;
        o.size = 0.55f;
        o.trail_len = 0.55 * 2.0 * M_PI / o.omega;
        o.trail_n = 70;
        o.label = "ISCO circular orbit";
        orbiters.push_back(o);
    }
    {
        Orbiter o;
        o.analytic_circle = true;
        o.r = std::max(9.0, risco * 3.4);
        o.omega = phys::circular(o.r, a).omega;
        o.phi0 = M_PI;
        o.color[0] = 1.0f; o.color[1] = 0.80f; o.color[2] = 0.35f;
        o.size = 0.55f;
        o.trail_len = 0.5 * 2.0 * M_PI / o.omega;
        o.trail_n = 80;
        o.label = "circular orbit";
        orbiters.push_back(o);
    }
    {
        // precessing eccentric orbit
        const double rp_ = std::max(1.6 * risco, 3.6), ra_ = 3.5 * rp_;
        double E, L;
        if (phys::bound_orbit_constants(a, rp_, ra_, &E, &L)) {
            Orbiter o;
            o.track = phys::integrate_track(a, E, L, ra_, true, 0.0, 6000.0);
            o.color[0] = 0.55f; o.color[1] = 0.85f; o.color[2] = 1.0f;
            o.size = 0.6f;
            o.trail_len = 260.0;
            o.trail_n = 180;
            o.label = "precessing orbit";
            orbiters.push_back(o);
        }
    }
    {
        // plunging particle: too little angular momentum to stay in orbit
        const double L = 0.72 * phys::circular(risco, a).L;
        const double r_s = risco * 2.1 + 1.0;
        const double E = phys::energy_from_apoapsis(a, r_s, L);
        if (E > 0.0) {
            Orbiter o;
            o.track = phys::integrate_track(a, E, L, r_s, true, 1.2, 4000.0);
            o.color[0] = 1.0f; o.color[1] = 0.42f; o.color[2] = 0.25f;
            o.size = 0.6f;
            o.trail_len = 60.0;
            o.trail_n = 90;
            o.plunge = o.track.plunged;
            o.hold = 22.0;
            o.label = "plunging particle";
            orbiters.push_back(o);
        }
    }

    // ---- light rays: equatorial null geodesics falling in from the left with different impact parameters ----
    photon_paths.clear();
    for (double L : {6.2, 3.6, -7.4, -9.5, -12.0}) {
        auto pp = phys::photon_path_equatorial(a, L, kFunnelRMax - 0.6, M_PI);
        if (pp.r.size() < 3) continue;
        std::vector<simd::float3> pts;
        double last_len = 0.0;
        float prev[3] = {0, 0, 0};
        for (size_t i = 0; i < pp.r.size(); ++i) {
            float q[3];
            embed_pos(pp.r[i], pp.phi[i], 0.15, q, 1.0);
            if (i > 0) {
                const double d = std::sqrt(double((q[0] - prev[0]) * (q[0] - prev[0]) + (q[1] - prev[1]) * (q[1] - prev[1]) + (q[2] - prev[2]) * (q[2] - prev[2])));
                last_len += d;
                if (last_len < 0.55 && i + 1 < pp.r.size()) continue;   // even spacing along the curve
                last_len = 0.0;
            }
            prev[0] = q[0]; prev[1] = q[1]; prev[2] = q[2];
            pts.push_back(simd::float3{q[0], q[1], q[2]});
        }
        photon_paths.push_back(pts);
    }
}

// =============================================================================================
// HUD (Core Text into a bitmap, uploaded as a texture a few times per second)
// =============================================================================================
static void hud_text(CGContextRef ctx, int h, NSString* s, double x, double ytop, double size, NSColor* col, bool bold = false)
{
    NSFont* f = [NSFont monospacedSystemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    NSDictionary* shadow = @{NSFontAttributeName : f, NSForegroundColorAttributeName : [NSColor colorWithCalibratedWhite:0 alpha:0.85]};
    NSDictionary* attr = @{NSFontAttributeName : f, NSForegroundColorAttributeName : col};
    const double y = h - ytop - size * 1.25;
    const double o = std::max(1.0, size / 14.0);
    [s drawAtPoint:NSMakePoint(x + o, y - o) withAttributes:shadow];
    [s drawAtPoint:NSMakePoint(x, y) withAttributes:attr];
}

static double text_width(NSString* s, double size)
{
    NSFont* f = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    return [s sizeWithAttributes:@{NSFontAttributeName : f}].width;
}

void Renderer::Impl::paint_hud(const SimState& s, int dw, int dh, double wall)
{
    const double ui = std::max(s.ui_scale, 1.0);
    const double fs = 12.5 * ui, lh = fs * 1.45, pad = 14 * ui;
    const double a = s.spin;
    const double rp = phys::r_horizon(a), risco = phys::r_isco(a), rph = phys::r_photon_pro(a);

    char l1[256], l2[256], l3[256], l4[256], l5[256];
    std::snprintf(l1, sizeof l1, "KERR BLACK HOLE   a/M = %.3f   spin axis: up", a);
    std::snprintf(l2, sizeof l2, "horizon r+ = %.3f M   photon orbit = %.3f M   ISCO = %.3f M   ergosphere r <= 2 M", rp, rph, risco);
    std::snprintf(l3, sizeof l3, "camera  r = %.1f M   theta = %.1f deg   phi = %.0f deg   FOV %.0f deg", s.dist, s.incl_deg, std::fmod(std::fmod(s.azim_deg, 360.0) + 360.0, 360.0), s.fov_deg);
    std::snprintf(l4, sizeof l4, "thin Novikov-Thorne disk   T_peak = %.0f K   t = %.1f M   x%.2f time", s.t_peak, s.sim_time, s.time_scale);
    const FrameInfo fi = finfo;
    std::snprintf(l5, sizeof l5, "%d x %d rays   %.1f ms GPU   %.0f fps", fi.internal_w, fi.internal_h, fi.gpu_ms, fps_ema);

    // Rebuilding the bitmap (Core Text + a 20 MB upload at Retina size) is expensive: rate-limit it to ~12 Hz,
    // except when the size or a transient status message changes.
    const bool status_live = !s.status_msg.empty() && wall < s.status_until;
    std::string key = std::string(l1) + l2 + l3 + std::to_string(s.funnel_mode) + std::to_string(s.hud) + std::to_string(s.debug_view);
    if (s.critical_curve) key += "curve" + std::to_string(s.spin) + "," + std::to_string(s.incl_deg) + "," + std::to_string(s.dist) + "," + std::to_string(s.fov_deg);
    const std::string slow_key = std::string(l4) + l5;     // clock / fps / GPU ms: refresh at 4 Hz only
    const std::string status_key = status_live ? s.status_msg : std::string();
    const bool size_changed = !hudTex || hud_w != dw || hud_h != dh;
    const bool status_changed = status_key != hud_status_key;
    const bool fast_changed = key != hud_key, slow_changed = slow_key != hud_slow_key;
    if (!size_changed && !status_changed) {
        if (!fast_changed && !slow_changed) return;
        if (fast_changed && wall - hud_built_at < 0.08) return;
        if (!fast_changed && slow_changed && wall - hud_built_at < 0.25) return;
    }
    hud_key = key;
    hud_slow_key = slow_key;
    hud_status_key = status_key;
    hud_built_at = wall;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const size_t bytesPerRow = (size_t)dw * 4;
    std::vector<uint8_t> buf(bytesPerRow * dh, 0);
    CGContextRef ctx = CGBitmapContextCreate(buf.data(), dw, dh, 8, bytesPerRow, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:g];

    auto panel = [&](double x, double ytop, double w, double h) {
        CGContextSetRGBFillColor(ctx, 0.0, 0.01, 0.03, 0.46);
        CGRect r = CGRectMake(x, dh - ytop - h, w, h);
        CGPathRef path = CGPathCreateWithRoundedRect(r, 8 * ui, 8 * ui, nullptr);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
        CGPathRelease(path);
    };
    NSColor* white = [NSColor colorWithCalibratedRed:0.92 green:0.95 blue:1.0 alpha:1];
    NSColor* dim = [NSColor colorWithCalibratedRed:0.62 green:0.72 blue:0.85 alpha:1];
    NSColor* accent = [NSColor colorWithCalibratedRed:1.0 green:0.78 blue:0.40 alpha:1];

    if (s.hud) {
        NSString* rows[5] = {[NSString stringWithUTF8String:l1], [NSString stringWithUTF8String:l2], [NSString stringWithUTF8String:l3],
                             [NSString stringWithUTF8String:l4], [NSString stringWithUTF8String:l5]};
        double wmax = 0;
        for (int i = 0; i < 5; ++i) wmax = std::max(wmax, text_width(rows[i], fs * (i == 0 ? 1.08 : 1.0)));
        panel(pad, pad, wmax + pad * 1.6, lh * 5 + pad * 0.9);
        double y = pad + pad * 0.45;
        hud_text(ctx, dh, rows[0], pad * 1.6, y, fs * 1.08, accent, true);
        y += lh * 1.08;
        hud_text(ctx, dh, rows[1], pad * 1.6, y, fs, white);
        y += lh;
        hud_text(ctx, dh, rows[2], pad * 1.6, y, fs, white);
        y += lh;
        hud_text(ctx, dh, rows[3], pad * 1.6, y, fs, dim);
        y += lh;
        hud_text(ctx, dh, rows[4], pad * 1.6, y, fs, dim);

        // key legend, bottom-left (kept narrow so it never runs under the funnel inset)
        const char* keys[] = {
            "drag: orbit camera     scroll: distance",
            "[ ]: spin a            , . : disk temperature",
            "space: pause           N M: time speed",
            "D disk  S stars  G grid  B bloom  V halo",
            "E turbulence  T anti-aliasing  O auto-orbit",
            "C: overlay analytic shadow curve",
            "F: funnel (inset / full / off)",
            "0-3: views   8 9: resolution   7: auto-res",
            "R reset   P screenshot   H hide   Q quit",
        };
        const int nk = 9;
        double kwmax = 0;
        for (int i = 0; i < nk; ++i) kwmax = std::max(kwmax, text_width([NSString stringWithUTF8String:keys[i]], fs * 0.96));
        const double kh = lh * 0.96 * nk + pad * 0.9;
        panel(pad, dh - pad - kh, kwmax + pad * 1.6, kh);
        for (int i = 0; i < nk; ++i)
            hud_text(ctx, dh, [NSString stringWithUTF8String:keys[i]], pad * 1.6, dh - pad - kh + pad * 0.45 + lh * 0.96 * i, fs * 0.96, dim);
    }
    if (s.critical_curve) {
        // Bardeen's critical curve (L, Q of circular photon orbits) mapped through THIS camera exactly.
        const double aa = std::max(s.spin, 1e-3), aspect = (double)dw / dh;
        CGContextSetRGBStrokeColor(ctx, 0.25, 0.95, 1.0, 0.95);
        CGContextSetLineWidth(ctx, std::max(1.5, 1.4 * ui));
        const double rp0 = phys::r_photon_pro(aa), rp1 = phys::r_photon_retro(aa);
        for (int branch = 0; branch < 2; ++branch) {
            bool pen = false;
            for (int i = 0; i <= 600; ++i) {
                double x, y;
                if (!bardeen_ndc(s, aspect, branch, rp0 + (rp1 - rp0) * i / 600.0, &x, &y)) { pen = false; continue; }
                const double px = (x * 0.5 + 0.5) * dw, py = (0.5 + y * 0.5) * dh;   // y up in this context
                if (!pen) { CGContextMoveToPoint(ctx, px, py); pen = true; }
                else CGContextAddLineToPoint(ctx, px, py);
            }
            CGContextStrokePath(ctx);
        }
        hud_text(ctx, dh, @"cyan: Bardeen (1973) analytic critical curve", dw * 0.5 - 20 * fs, dh - pad - fs * 2.2 - 0, fs * 0.95, [NSColor colorWithCalibratedRed:0.25 green:0.95 blue:1.0 alpha:1], true);
    }
    if (s.debug_view != 0) {
        const char* names[] = {"", "VIEW: image order (white = direct, cyan = 1st lensed, magenta = 2nd, green = 3rd+)",
                               "VIEW: redshift g (blue = blueshift, red = redshift)", "VIEW: integrator cost per ray"};
        hud_text(ctx, dh, [NSString stringWithUTF8String:names[s.debug_view]], pad * 1.6, pad * 1.0 + lh * 6.4, fs, accent, true);
    }
    // funnel legend (inset: under the sheet; full screen: bottom right)
    if (s.funnel_mode != 0 && s.hud) {
        struct Ent { const char* t; double r, g, b; };
        Ent ents[] = {{"horizon", 1.0, 0.32, 0.12}, {"ergosphere", 0.78, 0.42, 1.0}, {"photon orbit", 1.0, 0.94, 0.62},
                      {"ISCO", 0.35, 1.0, 0.55}, {"you", 1.0, 0.78, 0.38}, {"light rays", 1.0, 0.93, 0.68}};
        const double fsz = fs * 0.80;
        double w0 = 0, w1 = 0;
        for (int i = 0; i < 6; ++i) {
            NSString* t = [NSString stringWithFormat:@"\u25CF %s  ", ents[i].t];
            (i < 3 ? w0 : w1) += text_width(t, fsz);
        }
        const double rowh = fsz * 1.5;
        double x0, ybot;
        if (s.funnel_mode == 1 && inset[2] > inset[0]) {
            x0 = inset[0] + 10 * ui;
            ybot = inset[3] - 8 * ui;
            hud_text(ctx, dh, @"SPACETIME FUNNEL  (equatorial plane)", inset[0] + 10 * ui, inset[1] + 8 * ui, fs * 0.88, [NSColor colorWithCalibratedRed:0.55 green:0.85 blue:1.0 alpha:1], true);
        } else {
            const double w = std::max(w0, w1);
            x0 = dw - w - pad * 2;
            ybot = dh - pad;
            {
                NSString* cap = @"SPACETIME FUNNEL: embedding of the equatorial plane (true proper distances)";
                const double cw = text_width(cap, fsz);
                hud_text(ctx, dh, cap, dw - cw - pad * 1.6, dh - pad - rowh * 2 - fsz * 2.6, fsz, [NSColor colorWithCalibratedRed:0.55 green:0.85 blue:1.0 alpha:1], true);
            }
            panel(x0 - pad * 0.6, ybot - rowh * 2 - pad * 0.4, w + pad * 1.4, rowh * 2 + pad * 0.8);
        }
        double x = x0;
        for (int i = 0; i < 6; ++i) {
            if (i == 3) x = x0;
            NSColor* c = [NSColor colorWithCalibratedRed:ents[i].r green:ents[i].g blue:ents[i].b alpha:1];
            NSString* t = [NSString stringWithFormat:@"\u25CF %s  ", ents[i].t];
            hud_text(ctx, dh, t, x, ybot - rowh * (i < 3 ? 2 : 1) - fsz * 0.2, fsz, c);
            x += text_width(t, fsz);
        }
    }
    if (status_live) {
        NSString* m = [NSString stringWithUTF8String:s.status_msg.c_str()];
        const double w = (double)[m length] * fs * 0.62 + pad * 2;
        panel(dw * 0.5 - w * 0.5, dh * 0.5 - lh, w, lh * 2);
        hud_text(ctx, dh, m, dw * 0.5 - w * 0.5 + pad, dh * 0.5 - lh * 0.65, fs * 1.05, white, true);
    }
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(ctx);

    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:dw height:dh mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    hudTex = [dev newTextureWithDescriptor:d];
    [hudTex replaceRegion:MTLRegionMake2D(0, 0, dw, dh) mipmapLevel:0 withBytes:buf.data() bytesPerRow:bytesPerRow];
    hud_w = dw;
    hud_h = dh;
}

// =============================================================================================
// Renderer
// =============================================================================================
bool bardeen_ndc(const SimState& s, double aspect, int branch, double rr, double* x, double* y)
{
    const double aa = std::max(s.spin, 1e-3), r0 = std::max(s.dist, phys::r_horizon(aa) + 0.15);
    const double th0 = std::min(std::max(s.incl_deg, 3.0), 177.0) * M_PI / 180.0;
    const double mu = std::cos(th0), s2 = std::sin(th0) * std::sin(th0), a2 = aa * aa;
    const double Sigma = r0 * r0 + a2 * mu * mu, Delta = r0 * r0 - 2 * r0 + a2;
    const double A = (r0 * r0 + a2) * (r0 * r0 + a2) - a2 * Delta * s2;
    const double sg = std::sqrt(A * s2 / Sigma), lapse = std::sqrt(Sigma * Delta / A), om = 2 * aa * r0 / A;
    const double tanh_ = std::tan(s.fov_deg * M_PI / 360.0);
    double L, Q;
    phys::bardeen_LQ(aa, rr, &L, &Q);
    const double nph = -L * lapse / (sg * (1.0 - om * L));
    const double Ez = 1.0 / (lapse - om * sg * nph);
    const double Th = Q + a2 * mu * mu - L * L * mu * mu / s2;
    if (Th < 0 || Ez <= 0) return false;
    const double nth = (branch ? -1.0 : 1.0) * std::sqrt(Th) / (std::sqrt(Sigma) * Ez);
    const double nr2 = 1.0 - nph * nph - nth * nth;
    if (nr2 <= 0) return false;
    const double fwd = std::sqrt(nr2);
    *x = (nph / fwd) / (tanh_ * aspect);
    *y = (-nth / fwd) / tanh_;
    return true;
}

Renderer::Renderer() : p(new Impl) {}
Renderer::~Renderer() = default;

bool Renderer::init(id<MTLDevice> device, std::string* error)
{
    p->dev = device;
    return p->build_pipelines(error);
}

FrameInfo Renderer::info() const
{
    FrameInfo f = p->finfo;
    f.gpu_ms = p->gpu_ms.load();
    return f;
}
double Renderer::instant_fps() const { return p->fps_ema; }
void Renderer::note_frame_time(double dt)
{
    if (dt > 1e-4 && dt < 1.0) p->fps_ema = p->fps_ema * 0.9 + (1.0 / dt) * 0.1;
}
void Renderer::reset_history() { p->static_frames = 0; }

bool Renderer::in_inset(double x, double y) const
{
    return p->inset[2] > p->inset[0] && x >= p->inset[0] && x < p->inset[2] && y >= p->inset[1] && y < p->inset[3];
}

std::string state_summary(const SimState& s)
{
    char b[256];
    std::snprintf(b, sizeof b, "a=%.3f incl=%.1f dist=%.1f fov=%.1f", s.spin, s.incl_deg, s.dist, s.fov_deg);
    return b;
}

static void fill_rt_uniforms(RTUniforms& U, const SimState& s, int w, int h, double jx, double jy)
{
    const double a = std::min(std::max(s.spin, 0.0), 0.998);
    const double rplus = phys::r_horizon(a), risco = phys::r_isco(a);
    const double th0 = std::min(std::max(s.incl_deg, 3.0), 177.0) * M_PI / 180.0;
    const double r0 = std::max(s.dist, rplus + 0.15);
    U.cam = simd::float4{(float)r0, (float)th0, (float)(s.azim_deg * M_PI / 180.0), (float)std::tan(s.fov_deg * M_PI / 360.0)};
    U.bh = simd::float4{(float)a, (float)rplus, (float)risco, (float)s.disk_r_out};
    U.view = simd::float4{(float)w, (float)h, (float)s.sim_time, 0.f};
    U.jit = simd::float4{(float)jx, (float)jy, 0.f, 0.f};
    U.disk = simd::float4{(float)s.t_peak, s.disk ? 1.f : 0.f, s.halo ? (float)s.halo_amp : 0.f, s.stars ? 1.f : 0.f};
    U.opt = simd::float4{s.celestial_grid ? 1.f : 0.f, (float)s.debug_view, (float)s.step_coeff, 512.f};
    const double px = 2.0 * std::tan(s.fov_deg * M_PI / 360.0) / std::max(h, 1);
    U.sky = simd::float4{1.0f, (float)(0.75 * px), s.milky_way ? 1.f : 0.f, (float)std::max(400.0, 3.0 * r0)};
    // brightness normalised to the visible luminance of a T_peak blackbody: T_peak then changes the COLOUR, exposure the brightness
    const float disk_gain = (float)(1.0 / std::max(phys::blackbody_luminance(s.t_peak), 1e-9));
    U.misc = simd::float4{s.turbulence ? 1.f : 0.f, disk_gain * (float)s.disk_gain, 0.f, (float)s.disk_h};
}

void Renderer::encode_frame(id<MTLCommandBuffer> cb, id<MTLTexture> target, SimState& s, double dt, double wall)
{
    Impl& I = *p;
    s.spin = std::min(std::max(s.spin, 0.0), 0.998);
    s.dist = std::min(std::max(s.dist, 2.4), 1500.0);
    s.incl_deg = std::min(std::max(s.incl_deg, 3.0), 177.0);
    if (!s.paused) s.sim_time += dt * s.time_scale;
    if (s.auto_orbit) s.azim_deg += dt * 9.0;
    if (s.funnel_auto) s.funnel_yaw += dt * 0.10;

    const int dw = (int)target.width, dh = (int)target.height;
    int iw = std::max(64, (int)std::lround(dw * s.render_scale));
    int ih = std::max(48, (int)std::lround(dh * s.render_scale));

    // ---- dynamic resolution: only when the measured frame rate really is below target AND the GPU is the busy party
    // (with other apps competing for the GPU/compositor, shrinking further would not help, so never go below 0.5)
    const double gms = I.gpu_ms.load();
    if (s.auto_scale && gms > 0.0 && I.frame_index > 120 && --I.scale_cooldown <= 0) {
        const double budget = 1000.0 / s.target_fps * 0.9;
        if (I.fps_ema < s.target_fps * 0.92 && gms > budget * 0.8 && s.render_scale > 0.5) { s.render_scale = std::max(0.5, s.render_scale * 0.92); I.scale_cooldown = 30; }
        else if (I.fps_ema > s.target_fps * 0.97 && gms < budget * 0.65 && s.render_scale < 1.0) { s.render_scale = std::min(1.0, s.render_scale * 1.05); I.scale_cooldown = 45; }
    }
    iw = std::max(64, (int)std::lround(dw * s.render_scale));
    ih = std::max(48, (int)std::lround(dh * s.render_scale));
    I.ensure_targets(iw, ih);
    I.rebuild_tables(s.spin, s.disk_r_out);
    I.rebuild_funnel(s.spin);
    I.finfo.internal_w = iw;
    I.finfo.internal_h = ih;

    // ---- temporal accumulation bookkeeping
    uint64_t key = 1469598103934665603ULL;
    for (double v : {s.spin, s.incl_deg, s.azim_deg, s.dist, s.fov_deg, s.disk ? 1.0 : 0.0, s.stars ? 1.0 : 0.0, s.milky_way ? 1.0 : 0.0,
                     s.celestial_grid ? 1.0 : 0.0, s.turbulence ? 1.0 : 0.0, s.halo ? s.halo_amp : 0.0, s.t_peak, s.disk_r_out, (double)s.debug_view, (double)iw, (double)ih,
                     s.step_coeff, s.taa ? 1.0 : 0.0})
        key = hash_mix(key, v);
    const bool changed = (key != I.last_key);
    I.last_key = key;
    if (changed || !s.taa) I.static_frames = 0; else I.static_frames++;
    const int sf = I.static_frames;
    const float a_static = (!s.taa || sf == 0) ? 1.0f : std::max(1.0f / (float)(sf + 1), 1.0f / 48.0f);
    const bool animating = !s.paused && s.disk && s.turbulence;
    const float a_moving = (a_static >= 1.0f) ? 1.0f : (animating ? 0.6f : a_static);
    double jx = 0, jy = 0;
    if (s.taa && a_static < 1.0f) {
        jx = halton(sf % 32 + 1, 2) - 0.5;
        jy = halton(sf % 32 + 1, 3) - 0.5;
    }

    // ---- 1. ray tracing
    RTUniforms RU;
    fill_rt_uniforms(RU, s, iw, ih, jx, jy);
    RU.view.w = (float)I.frame_index;
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent];
    [ce setComputePipelineState:I.psRaytrace];
    [ce setBytes:&RU length:sizeof RU atIndex:0];
    [ce setBuffer:I.diskBuf offset:0 atIndex:1];
    [ce setBuffer:I.bbBuf offset:0 atIndex:2];
    [ce setTexture:I.cur atIndex:0];
    [ce dispatchThreads:MTLSizeMake(iw, ih, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];

    // ---- 2. temporal resolve
    PostUniforms PU;
    PU.a = simd::float4{a_static, a_moving, (float)s.exposure, s.bloom ? 0.10f : 0.0f};
    PU.b = simd::float4{(float)iw, (float)ih, (float)dw, (float)dh};
    PU.c = simd::float4{0, 0, 0, 0};
    PU.d = simd::float4{s.funnel_mode == 2 ? 1.f : 0.f, (s.hud || s.critical_curve) ? 1.f : 0.f, 0.55f, 1.05f};
    PU.e = simd::float4{(float)s.sim_time, (float)I.frame_index, iw < dw * 0.98 ? 1.f : 0.f, 0.f};
    const int src = I.hist_idx, dst = 1 - I.hist_idx;
    [ce setComputePipelineState:I.psTaa];
    [ce setBytes:&PU length:sizeof PU atIndex:0];
    [ce setTexture:I.cur atIndex:0];
    [ce setTexture:I.hist[src] atIndex:1];
    [ce setTexture:I.hist[dst] atIndex:2];
    [ce dispatchThreads:MTLSizeMake(iw, ih, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    I.hist_idx = dst;

    // ---- 3. bloom
    if (s.bloom) {
        id<MTLTexture> in = I.hist[I.hist_idx];
        for (int i = 0; i < kBloomLevels; ++i) {
            simd::float4 prm{i == 0 ? 1.f : 0.f, 0, 0, 0};
            [ce setComputePipelineState:I.psBloomDown];
            [ce setTexture:in atIndex:0];
            [ce setTexture:I.down[i] atIndex:1];
            [ce setBytes:&prm length:sizeof prm atIndex:0];
            [ce dispatchThreads:MTLSizeMake(I.down[i].width, I.down[i].height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            in = I.down[i];
        }
        for (int i = kBloomLevels - 2; i >= 0; --i) {
            simd::float4 prm{0.72f, 0, 0, 0};
            id<MTLTexture> small = (i == kBloomLevels - 2) ? I.down[kBloomLevels - 1] : I.up[i + 1];
            [ce setComputePipelineState:I.psBloomUp];
            [ce setTexture:small atIndex:0];
            [ce setTexture:I.down[i] atIndex:1];
            [ce setTexture:I.up[i] atIndex:2];
            [ce setBytes:&prm length:sizeof prm atIndex:0];
            [ce dispatchThreads:MTLSizeMake(I.up[i].width, I.up[i].height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        }
    }
    [ce endEncoding];

    // ---- 4. spacetime funnel
    const bool funnel_on = s.funnel_mode != 0;
    if (funnel_on) {
        int fw_, fh_;
        if (s.funnel_mode == 2) {
            fw_ = dw;
            fh_ = dh;
            I.inset[0] = I.inset[1] = I.inset[2] = I.inset[3] = 0;
        } else {
            const double ui = std::max(s.ui_scale, 1.0);
            fw_ = (int)std::min(std::max(dw * 0.32, 300.0), 1100.0 * ui / 2.0 + 300.0);
            fh_ = (int)(fw_ * 0.78);
            const double m = 16 * ui;
            I.inset[2] = (float)(dw - m);
            I.inset[0] = I.inset[2] - fw_;
            I.inset[3] = (float)(dh - m);
            I.inset[1] = I.inset[3] - fh_;
        }
        I.ensure_funnel_targets(fw_, fh_);

        FunnelUniforms FU;
        const double a = s.spin;
        const double rp = phys::r_horizon(a);
        const float3 target = float3{0.f, 0.f, (float)(I.z_min * 0.40)};
        const float yaw = (float)s.funnel_yaw, pitch = (float)s.funnel_pitch;
        const float dist = (float)s.funnel_dist * (s.funnel_mode == 2 ? 1.22f : 1.0f);
        const float3 eye = target + dist * float3{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        const float3 up = {0, 0, 1};
        const float4x4 V = look_at(eye, target, up);
        const float aspect = (float)fw_ / (float)fh_;
        const float4x4 Pm = perspective(0.52f, aspect, 2.0f, 600.0f);
        FU.viewProj = Pm * V;
        FU.camPos = simd::float4{eye.x, eye.y, eye.z, aspect};
        FU.bh = RU.bh;
        FU.radii = simd::float4{(float)phys::r_photon_pro(a), 2.0f, (float)std::max(s.dist, rp + 0.2), (float)(s.azim_deg * M_PI / 180.0)};
        FU.time = simd::float4{(float)s.sim_time, (float)s.t_peak, (float)fw_, (float)fh_};
        FU.opt = simd::float4{2.0f, 1.0f, (float)std::max(-I.z_min, 1.0), s.disk ? 1.f : 0.f};
        FU.camRight = simd::float4{V.columns[0].x, V.columns[1].x, V.columns[2].x, 0.f};
        FU.camUp = simd::float4{V.columns[0].y, V.columns[1].y, V.columns[2].y, 0.f};
        FU.camRight.w = RU.misc.y;   // disk brightness gain (same photometric scale as the ray tracer)

        // particles
        std::vector<ParticleVertex> parts;
        parts.reserve(1200);
        const double t = s.sim_time;
        for (const Orbiter& o : I.orbiters) {
            auto pos_at = [&](double tt, float out[3], double* fade) {
                *fade = 1.0;
                if (o.analytic_circle) {
                    I.embed_pos(o.r, o.phi0 + o.omega * tt, 0.28, out, 1.0);
                    return;
                }
                const double T = o.track.duration();
                if (o.plunge) {
                    const double period = T + o.hold;
                    double tl = std::fmod(tt, period);
                    if (tl < 0) tl += period;
                    if (tl <= T) {
                        double r, ph;
                        o.track.sample(tl, false, &r, &ph);
                        I.embed_pos(r, ph, 0.28, out, 1.0);
                    } else {
                        double r, ph;
                        o.track.sample(T, false, &r, &ph);
                        double rho, z;
                        I.embed_lookup(r, &rho, &z);
                        const double ta = tl - T;
                        const double rr = rho;
                        out[0] = (float)(rr * std::cos(ph));
                        out[1] = (float)(rr * std::sin(ph));
                        out[2] = (float)(z - 0.35 * ta);
                        *fade = std::exp(-ta / 9.0);
                    }
                } else {
                    double r, ph;
                    o.track.sample(tt, false, &r, &ph);
                    I.embed_pos(r, ph, 0.28, out, 1.0);
                }
            };
            for (int k = o.trail_n; k >= 0; --k) {
                const double tt = t - o.trail_len * k / o.trail_n;
                if (tt < 0.0 && !o.analytic_circle) continue;
                float pp[3];
                double fade;
                pos_at(tt, pp, &fade);
                const double age = (double)k / o.trail_n;
                ParticleVertex pv;
                if (k == 0) {
                    pv.pos = simd::float4{pp[0], pp[1], pp[2], o.size * 2.6f};
                    pv.color = simd::float4{o.color[0], o.color[1], o.color[2], (float)(3.6 * fade)};
                } else {
                    const float w = (float)std::pow(1.0 - age, 1.6);
                    pv.pos = simd::float4{pp[0], pp[1], pp[2], o.size * (0.35f + 0.5f * w)};
                    pv.color = simd::float4{o.color[0], o.color[1], o.color[2], (float)(1.0 * w * fade)};
                }
                parts.push_back(pv);
            }
        }
        // light rays (equatorial null geodesics): dots flowing inward
        for (const auto& path : I.photon_paths) {
            const size_t n = path.size();
            for (size_t k = 0; k < n; ++k) {
                const float ph = (float)k * 0.42f - (float)t * 2.4f;
                const float pulse = 0.35f + 0.65f * std::pow(0.5f + 0.5f * std::sin(ph), 3.0f);
                ParticleVertex pv;
                pv.pos = simd::float4{path[k].x, path[k].y, path[k].z, 0.17f + 0.10f * pulse};
                pv.color = simd::float4{1.0f, 0.93f, 0.68f, 0.55f + 1.4f * pulse};
                parts.push_back(pv);
            }
        }
        {   // the observer: a marker on a pole standing on the sheet
            float pp[3];
            I.embed_pos(std::max(s.dist, rp + 0.05), s.azim_deg * M_PI / 180.0, 0.0, pp, 1.0);
            for (int k = 0; k < 9; ++k) {
                ParticleVertex pv;
                pv.pos = simd::float4{pp[0], pp[1], pp[2] + 0.4f + k * 0.55f, 0.28f};
                pv.color = simd::float4{1.0f, 0.78f, 0.38f, 1.0f - 0.07f * k};
                parts.push_back(pv);
            }
            ParticleVertex pv;
            pv.pos = simd::float4{pp[0], pp[1], pp[2] + 5.4f, 1.5f};
            pv.color = simd::float4{1.0f, 0.80f, 0.42f, 3.6f};
            parts.push_back(pv);
        }
        id<MTLBuffer> pbuf = [I.dev newBufferWithBytes:parts.data() length:parts.size() * sizeof(ParticleVertex) options:MTLResourceStorageModeShared];

        MTLRenderPassDescriptor* rp_ = [MTLRenderPassDescriptor renderPassDescriptor];
        rp_.colorAttachments[0].texture = I.funnelMsaa;
        rp_.colorAttachments[0].resolveTexture = I.funnelTex;
        rp_.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp_.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        rp_.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        rp_.depthAttachment.texture = I.funnelDepth;
        rp_.depthAttachment.loadAction = MTLLoadActionClear;
        rp_.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp_.depthAttachment.clearDepth = 1.0;
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp_];
        [re setRenderPipelineState:I.psFunnel];
        [re setDepthStencilState:I.dsMesh];
        [re setVertexBuffer:I.fVerts offset:0 atIndex:0];
        [re setVertexBytes:&FU length:sizeof FU atIndex:1];
        [re setFragmentBytes:&FU length:sizeof FU atIndex:1];
        [re setFragmentBuffer:I.diskBuf offset:0 atIndex:2];
        [re setFragmentBuffer:I.bbBuf offset:0 atIndex:3];
        [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:I.fIdxCount indexType:MTLIndexTypeUInt32 indexBuffer:I.fIdx indexBufferOffset:0];
        [re setRenderPipelineState:I.psParticle];
        [re setDepthStencilState:I.dsParticle];
        [re setVertexBuffer:pbuf offset:0 atIndex:0];
        [re setVertexBytes:&FU length:sizeof FU atIndex:1];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:parts.size()];
        [re endEncoding];
    } else {
        I.inset[0] = I.inset[1] = I.inset[2] = I.inset[3] = 0;
    }

    // ---- 5. HUD + present
    I.finfo.gpu_ms = I.gpu_ms.load();
    if (s.hud || s.critical_curve || s.status_until > wall) I.paint_hud(s, dw, dh, wall);
    if (s.funnel_mode == 1) PU.c = simd::float4{I.inset[0], I.inset[1], I.inset[2], I.inset[3]};
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> pe = [cb renderCommandEncoderWithDescriptor:pass];
    [pe setRenderPipelineState:I.psPresent];
    [pe setFragmentBytes:&PU length:sizeof PU atIndex:0];
    [pe setFragmentTexture:I.hist[I.hist_idx] atIndex:0];
    [pe setFragmentTexture:(s.bloom ? I.up[0] : I.black1) atIndex:1];
    [pe setFragmentTexture:(funnel_on ? I.funnelTex : I.black1) atIndex:2];
    [pe setFragmentTexture:(I.hudTex ? I.hudTex : I.black1) atIndex:3];
    [pe drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [pe endEncoding];

    I.frame_index++;
    Impl* ip = &I;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        const double ms = (c.GPUEndTime - c.GPUStartTime) * 1000.0;
        if (ms > 0.0 && ms < 1000.0) {
            const double old = ip->gpu_ms.load();
            ip->gpu_ms.store(old <= 0.0 ? ms : old * 0.85 + ms * 0.15);
        }
    }];
}

// =============================================================================================
// GPU probe (validation)
// =============================================================================================
bool Renderer::gpu_probe(const SimState& s, const std::vector<float>& xy, std::vector<float>* out, int width, int height)
{
    Impl& I = *p;
    const size_t n = xy.size() / 2;
    I.rebuild_tables(s.spin, s.disk_r_out);
    RTUniforms RU;
    fill_rt_uniforms(RU, s, width, height, 0, 0);
    id<MTLBuffer> in = [I.dev newBufferWithBytes:xy.data() length:xy.size() * sizeof(float) options:MTLResourceStorageModeShared];
    id<MTLBuffer> ob = [I.dev newBufferWithLength:n * 12 * sizeof(float) options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> q = [I.dev newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:I.psProbe];
    [ce setBytes:&RU length:sizeof RU atIndex:0];
    [ce setBuffer:in offset:0 atIndex:1];
    [ce setBuffer:ob offset:0 atIndex:2];
    [ce dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error) return false;
    out->assign((float*)ob.contents, (float*)ob.contents + n * 12);
    return true;
}
