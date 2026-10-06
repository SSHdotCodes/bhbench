// main.mm -- window, input, command line, screenshots, benchmarks and the GPU-vs-CPU parity test.
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "kerr_shared.h"
#include "physics.hpp"
#include "renderer.h"
#include "selftest.hpp"

// =============================================================================================
// Options
// =============================================================================================
struct Options {
    SimState state;
    bool selftest = false, gputest = false, bench = false;
    std::string shot;            // headless screenshot path
    std::string window_shot;     // screenshot of the live window
    int width = 1600, height = 1000;
    int frames = 48;
    double auto_quit = 0.0;      // seconds
    bool paused_set = false;
    bool fullscreen = false;
};

static bool parse_args(int argc, char** argv, Options& o)
{
    auto need = [&](int& i) -> const char* {
        if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", argv[i]); std::exit(2); }
        return argv[++i];
    };
    SimState& s = o.state;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--selftest") o.selftest = true;
        else if (a == "--gputest") o.gputest = true;
        else if (a == "--bench") o.bench = true;
        else if (a == "--shot") o.shot = need(i);
        else if (a == "--window-shot") o.window_shot = need(i);
        else if (a == "--size") { if (std::sscanf(need(i), "%dx%d", &o.width, &o.height) != 2) return false; }
        else if (a == "--frames") o.frames = std::atoi(need(i));
        else if (a == "--auto-quit") o.auto_quit = std::atof(need(i));
        else if (a == "--spin") s.spin = std::atof(need(i));
        else if (a == "--incl") s.incl_deg = std::atof(need(i));
        else if (a == "--azim") s.azim_deg = std::atof(need(i));
        else if (a == "--dist") s.dist = std::atof(need(i));
        else if (a == "--fov") s.fov_deg = std::atof(need(i));
        else if (a == "--tpeak") s.t_peak = std::atof(need(i));
        else if (a == "--exposure") s.exposure = std::atof(need(i));
        else if (a == "--rout") s.disk_r_out = std::atof(need(i));
        else if (a == "--thick") s.disk_h = std::atof(need(i));
        else if (a == "--gain") s.disk_gain = std::atof(need(i));
        else if (a == "--scale") { s.render_scale = std::atof(need(i)); s.auto_scale = false; }
        else if (a == "--time") s.sim_time = std::atof(need(i));
        else if (a == "--speed") s.time_scale = std::atof(need(i));
        else if (a == "--view") s.debug_view = std::atoi(need(i));
        else if (a == "--funnel") s.funnel_mode = std::atoi(need(i));
        else if (a == "--funnel-yaw") { s.funnel_yaw = std::atof(need(i)); s.funnel_auto = false; }
        else if (a == "--funnel-pitch") s.funnel_pitch = std::atof(need(i));
        else if (a == "--step") s.step_coeff = std::atof(need(i));
        else if (a == "--paused") s.paused = true;
        else if (a == "--no-disk") s.disk = false;
        else if (a == "--no-stars") s.stars = false;
        else if (a == "--no-milkyway") s.milky_way = false;
        else if (a == "--no-bloom") s.bloom = false;
        else if (a == "--no-turbulence") s.turbulence = false;
        else if (a == "--no-halo") s.halo = false;
        else if (a == "--halo") s.halo_amp = std::atof(need(i));
        else if (a == "--no-hud") s.hud = false;
        else if (a == "--no-taa") s.taa = false;
        else if (a == "--grid") s.celestial_grid = true;
        else if (a == "--orbit") s.auto_orbit = true;
        else if (a == "--curve") s.critical_curve = true;
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--help" || a == "-h") return false;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    return true;
}

static void usage()
{
    std::puts(
        "Kerr black hole -- real-time Metal ray tracer\n"
        "  blackhole                       open the interactive window\n"
        "  blackhole --selftest            run the physics validation suite (CPU + GPU parity)\n"
        "  blackhole --shot out.png        render offscreen and save a PNG (see --size, --frames)\n"
        "  blackhole --bench               GPU timing benchmark\n"
        "options: --spin a --incl deg --azim deg --dist M --fov deg --tpeak K --exposure x --rout M\n"
        "         --scale s (internal resolution, disables auto-scale) --time t --speed x --paused --view 0..3\n"
        "         --funnel 0|1|2 --funnel-yaw rad --funnel-pitch rad --step c --no-disk --no-stars --no-milkyway\n"
        "         --no-bloom --no-turbulence --no-hud --no-taa --grid --curve --orbit --size WxH --frames N --auto-quit sec\n"
        "         --window-shot out.png (capture the live window then exit)");
}

// =============================================================================================
// Screenshot helpers
// =============================================================================================
static id<MTLTexture> make_shot_texture(id<MTLDevice> dev, int w, int h)
{
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    return [dev newTextureWithDescriptor:d];
}

static bool save_png(id<MTLTexture> t, const std::string& path)
{
    const int w = (int)t.width, h = (int)t.height;
    std::vector<uint8_t> px((size_t)w * h * 4);
    [t getBytes:px.data() bytesPerRow:(size_t)w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, (size_t)w * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return ok;
}

// =============================================================================================
// GPU vs CPU parity: the Metal shader and the double-precision CPU code trace the same rays.
// =============================================================================================
static int run_gpu_parity(id<MTLDevice> dev, Renderer& R)
{
    std::printf("\n=== GPU (float32, Metal) vs CPU (double) parity on %s ===\n", [[dev name] UTF8String]);
    struct Cfg { double a, incl, dist; };
    int failures = 0;
    for (Cfg cfg : {Cfg{0.9, 76.0, 46.0}, Cfg{0.0, 85.0, 30.0}, Cfg{0.998, 30.0, 20.0}}) {
        SimState s;
        s.spin = cfg.a; s.incl_deg = cfg.incl; s.dist = cfg.dist; s.azim_deg = 0.0;
        const int W = 128, H = 80;
        std::vector<float> xy;
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                xy.push_back(((i + 0.37f) / W) * 2.0f - 1.0f);
                xy.push_back(-(((j + 0.61f) / H) * 2.0f - 1.0f));
            }
        std::vector<float> g;
        if (!R.gpu_probe(s, xy, &g, W, H)) { std::puts("  [FAIL] GPU probe failed"); return 1; }

        const double a = cfg.a, th0 = cfg.incl * M_PI / 180.0, r0 = cfg.dist;
        const double tanh_ = std::tan(s.fov_deg * M_PI / 360.0), aspect = (double)W / H;
        bh::TraceCfg<double> tc;
        tc.a = a;
        tc.r_plus = phys::r_horizon(a);
        tc.r_isco = phys::r_isco(a);
        tc.r_out = s.disk_r_out;
        tc.r_far = std::max(400.0, 3.0 * r0);
        tc.c = s.step_coeff;
        tc.max_steps = 512;
        tc.disk = 1;
        tc.disk_h = s.disk_h;
        tc.halo = 0.0;
        tc.nu_cam = 1.0;
        int same_status = 0, both_disk = 0, both_esc = 0, total = 0;
        double max_r = 0, max_g = 0, max_dir = 0, max_t = 0, max_phi = 0;
        int steps_gpu = 0;
        std::vector<double> dir_errs, phi_errs, r_errs, g_errs;
        int worst_k = -1;
        for (size_t k = 0; k < xy.size() / 2; ++k) {
            const double nx = xy[2 * k] * aspect * tanh_, ny = xy[2 * k + 1] * tanh_, nz = 1.0;
            const double inv = 1.0 / std::sqrt(nx * nx + ny * ny + nz * nz);
            bh::Ray<double> ray;
            double nu;
            const float* o = &g[12 * k];
            ++total;
            if (!bh::init_ray_zamo<double>(a, r0, th0, 0.0, -nz * inv, -ny * inv, nx * inv, ray, nu)) { if (o[0] < 0) ++same_status; continue; }
            bh::TraceOut<double> out;
            bh::trace_ray<double>(tc, ray, out);
            steps_gpu += (int)o[1];
            if ((int)o[0] == out.status) ++same_status;
            if ((int)o[0] == 2 && out.status == 2) {
                ++both_disk;
                max_r = std::max(max_r, std::fabs(o[3] - out.r_hit) / out.r_hit);
                r_errs.push_back(std::fabs(o[3] - out.r_hit) / out.r_hit);
                const double dp = std::fabs(std::remainder((double)o[4] - out.phi_hit, 2 * M_PI));
                max_phi = std::max(max_phi, dp);
                phi_errs.push_back(dp);
                max_t = std::max(max_t, std::fabs(o[5] - out.t_hit));
                const double gc = bh::disk_redshift<double>(out.r_hit, a, out.L, nu);
                max_g = std::max(max_g, std::fabs(o[11] - gc) / gc);
                g_errs.push_back(std::fabs(o[11] - gc) / gc);
            }
            if ((int)o[0] == 1 && out.status == 1) {
                ++both_esc;
                const double ex = o[8] - out.dx, ey = o[9] - out.dy, ez = o[10] - out.dz;
                const double de = std::sqrt(ex * ex + ey * ey + ez * ez);   // chord length ~ angle (acos(dot) would amplify rounding)
                if (de > max_dir) { max_dir = de; worst_k = (int)k; }
                dir_errs.push_back(de);
            }
        }
        auto pct = [](std::vector<double> v, double q) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[(size_t)(q * (v.size() - 1))]; };
        std::printf("      escape-direction error: median %.1e  p99 %.1e  p99.9 %.1e  max %.1e rad;  disk phi error: median %.1e p99 %.1e p99.9 %.1e\n",
                    pct(dir_errs, 0.5), pct(dir_errs, 0.99), pct(dir_errs, 0.999), max_dir, pct(phi_errs, 0.5), pct(phi_errs, 0.99), pct(phi_errs, 0.999));
        if (worst_k >= 0) {
            std::printf("      worst escape pixel: (%d, %d)\n", worst_k % W, worst_k / W);
            const float* o = &g[12 * worst_k];
            std::printf("      GPU: status %d steps %d L %.7f dir (%.7f %.7f %.7f) nu %.7f\n", (int)o[0], (int)o[1], o[6], o[8], o[9], o[10], o[7]);
        }
        // (a few grazing equatorial crossings amplify float rounding, so disk metrics are judged at the 99.9th percentile)
        const bool ok = same_status >= total * 0.995 && pct(r_errs, 0.999) < 1e-3 && pct(dir_errs, 0.999) < 2e-3 && pct(g_errs, 0.999) < 2e-3 && pct(phi_errs, 0.999) < 2e-3;
        std::printf("  [%s] a=%.3f i=%.0f r0=%.0f : status agree %d/%d, disk hits %d (p99.9 dr/r %.1e, dphi %.1e rad, max dt %.2e M, p99.9 dg/g %.1e), "
                    "escapes %d (p99.9 dir %.1e rad), %.1f steps/ray\n",
                    ok ? "PASS" : "FAIL", a, cfg.incl, r0, same_status, total, both_disk, pct(r_errs, 0.999), pct(phi_errs, 0.999), max_t, pct(g_errs, 0.999),
                    both_esc, pct(dir_errs, 0.999), (double)steps_gpu / total);
        if (!ok) ++failures;
    }
    // ---- shadow edge on the GPU vs Bardeen's analytic curve, through the exact camera mapping ----
    struct SCfg { double a, incl, dist; };
    for (SCfg c : {SCfg{0.9, 60.0, 50.0}, SCfg{0.5, 30.0, 40.0}, SCfg{0.998, 80.0, 35.0}, SCfg{0.0, 45.0, 60.0}}) {
        SimState s;
        s.spin = std::max(c.a, 0.003); s.incl_deg = c.incl; s.dist = c.dist; s.disk = false; s.fov_deg = 30.0;
        const int W = 1400, H = 875;
        const double aspect = (double)W / H;
        std::vector<std::array<double, 2>> pts;
        const double rp0 = phys::r_photon_pro(s.spin), rp1 = phys::r_photon_retro(s.spin);
        for (int br = 0; br < 2; ++br)
            for (int i = 1; i < 120; ++i) {
                double x, y;
                if (bardeen_ndc(s, aspect, br, rp0 + (rp1 - rp0) * i / 120.0, &x, &y)) pts.push_back({x, y});
            }
        double cx = 0, cy = 0;
        for (auto& p : pts) { cx += p[0]; cy += p[1]; }
        cx /= pts.size(); cy /= pts.size();
        std::vector<float> xy;
        const double eps = 0.004;   // 0.4% of the shadow radius (~1 px)
        for (auto& p : pts)
            for (double f : {1.0 - eps, 1.0 + eps}) {
                xy.push_back((float)(cx + (p[0] - cx) * f));
                xy.push_back((float)(cy + (p[1] - cy) * f));
            }
        std::vector<float> g;
        R.gpu_probe(s, xy, &g, W, H);
        int good = 0;
        for (size_t k = 0; k < pts.size(); ++k) {
            const int st_in = (int)g[12 * (2 * k)], st_out = (int)g[12 * (2 * k + 1)];
            if (st_in == 0 && st_out == 1) ++good;
        }
        const bool ok = good >= (int)(pts.size() * 0.98);
        std::printf("  [%s] GPU shadow edge vs Bardeen curve, a=%.3f i=%.0f r0=%.0f: %d/%zu points bracketed by captured (inside) / escaped (outside) rays at +-0.4%%\n",
                    ok ? "PASS" : "FAIL", c.a, c.incl, c.dist, good, pts.size());
        if (!ok) ++failures;
    }
    return failures;
}

// =============================================================================================
// Headless render / benchmark
// =============================================================================================
static int run_headless(Options& o, id<MTLDevice> dev, Renderer& R)
{
    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLTexture> tex = make_shot_texture(dev, o.width, o.height);
    SimState& s = o.state;
    s.ui_scale = std::max(1.0, o.width / 900.0);
    s.auto_scale = false;
    const double dt = 1.0 / 60.0;
    double wall = 0.0;
    double gsum = 0;
    int gn = 0;
    for (int f = 0; f < o.frames; ++f) {
        id<MTLCommandBuffer> cb = [q commandBuffer];
        R.encode_frame(cb, tex, s, s.paused ? 0.0 : dt, wall);
        wall += dt;
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.error) { std::fprintf(stderr, "GPU error: %s\n", [[cb.error localizedDescription] UTF8String]); return 1; }
        if (f >= o.frames / 4) { gsum += (cb.GPUEndTime - cb.GPUStartTime) * 1000.0; ++gn; }
    }
    if (o.bench) {
        std::printf("%dx%d internal %dx%d: %.2f ms GPU/frame (%.0f fps GPU-bound) [%s]\n", o.width, o.height, R.info().internal_w, R.info().internal_h,
                    gsum / std::max(gn, 1), 1000.0 / (gsum / std::max(gn, 1)), state_summary(s).c_str());
    }
    if (!o.shot.empty()) {
        if (!save_png(tex, o.shot)) { std::fprintf(stderr, "could not write %s\n", o.shot.c_str()); return 1; }
        std::printf("wrote %s (%dx%d)\n", o.shot.c_str(), o.width, o.height);
    }
    return 0;
}

// =============================================================================================
// Interactive window
// =============================================================================================
@interface BHView : MTKView <MTKViewDelegate>
@end

@implementation BHView {
    std::unique_ptr<Renderer> _renderer;
    SimState _s;
    id<MTLCommandQueue> _queue;
    double _last;
    double _t0;
    NSPoint _mouse;
    bool _dragFunnel;
    Options _opt;
    id<MTLTexture> _shotTex;
    bool _shotPending;
    int _frame;
    int _fpsFrames;
    double _fpsT0;
    double _cpuSum;
}

- (instancetype)initWithFrame:(NSRect)r device:(id<MTLDevice>)dev options:(const Options&)opt
{
    self = [super initWithFrame:r device:dev];
    if (!self) return nil;
    _opt = opt;
    _s = opt.state;
    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.framebufferOnly = YES;
    self.preferredFramesPerSecond = 60;
    self.delegate = self;
    self.paused = NO;
    self.enableSetNeedsDisplay = NO;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    self.colorspace = cs;
    CGColorSpaceRelease(cs);
    _queue = [dev newCommandQueue];
    _renderer.reset(new Renderer());
    std::string err;
    if (!_renderer->init(dev, &err)) {
        std::fprintf(stderr, "renderer init failed: %s\n", err.c_str());
        return nil;
    }
    _last = CACurrentMediaTime();
    _t0 = _last;
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size { _renderer->reset_history(); }

- (void)status:(NSString*)m
{
    _s.status_msg = [m UTF8String];
    _s.status_until = CACurrentMediaTime() + 1.6;
}

- (void)drawInMTKView:(MTKView*)view
{
    const double now = CACurrentMediaTime();
    double dt = now - _last;
    _last = now;
    if (dt > 0.25) dt = 0.25;
    _renderer->note_frame_time(dt);
    _s.ui_scale = self.window ? self.window.backingScaleFactor : 2.0;
    id<CAMetalDrawable> drawable = self.currentDrawable;
    if (!drawable) return;
    id<MTLCommandBuffer> cb = [_queue commandBuffer];

    const bool wantShot = !_opt.window_shot.empty() && (now - _t0) > (_opt.auto_quit > 0 ? _opt.auto_quit * 0.6 : 3.0) && !_shotPending;
    if (wantShot) {
        // render one extra frame into a CPU-readable texture (same pipeline, same state), then present as usual
        const int w = (int)drawable.texture.width, h = (int)drawable.texture.height;
        if (!_shotTex || (int)_shotTex.width != w || (int)_shotTex.height != h) _shotTex = make_shot_texture(self.device, w, h);
        _renderer->encode_frame(cb, _shotTex, _s, 0.0, now);
        _shotPending = true;
        id<MTLTexture> t = _shotTex;
        std::string path = _opt.window_shot;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
            save_png(t, path);
            std::printf("window capture written to %s (%dx%d)\n", path.c_str(), (int)t.width, (int)t.height);
        }];
        [cb commit];
        cb = [_queue commandBuffer];
    }
    const double c0 = CACurrentMediaTime();
    _renderer->encode_frame(cb, drawable.texture, _s, dt, now);
    if (now - _t0 > 2.5) _cpuSum += CACurrentMediaTime() - c0;
    [cb presentDrawable:drawable];
    [cb commit];
    _frame++;
    if (now - _t0 > 2.5) {   // steady-state frame rate for the log line printed at exit
        if (_fpsFrames == 0) _fpsT0 = now;
        _fpsFrames++;
    }

    if (_opt.auto_quit > 0.0 && (now - _t0) > _opt.auto_quit) {
        [cb waitUntilCompleted];
        if (_fpsFrames > 10)
            std::printf("window: %.1f fps average over %.1f s, CPU encode %.2f ms/frame  (%dx%d drawable, internal %dx%d, %.1f ms GPU)\n", _fpsFrames / (now - _fpsT0), now - _fpsT0, 1000.0 * _cpuSum / _fpsFrames,
                        (int)drawable.texture.width, (int)drawable.texture.height, _renderer->info().internal_w, _renderer->info().internal_h, _renderer->info().gpu_ms);
        [NSApp terminate:nil];
    }
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------
- (NSPoint)backingPoint:(NSEvent*)e
{
    NSPoint p = [self convertPointToBacking:[self convertPoint:e.locationInWindow fromView:nil]];
    return NSMakePoint(p.x, self.drawableSize.height - p.y);   // origin top-left
}

- (void)mouseDown:(NSEvent*)e
{
    NSPoint p = [self backingPoint:e];
    _mouse = p;
    _dragFunnel = _s.funnel_mode == 2 || _renderer->in_inset(p.x, p.y);
    if (e.clickCount == 2) {
        _s.funnel_auto = true;
        [self status:@"funnel: auto-rotate"];
    }
}

- (void)mouseDragged:(NSEvent*)e
{
    NSPoint p = [self backingPoint:e];
    const double dx = p.x - _mouse.x, dy = p.y - _mouse.y;
    _mouse = p;
    const double k = 1.0 / (self.window ? self.window.backingScaleFactor : 2.0);
    if (_dragFunnel) {
        _s.funnel_auto = false;
        _s.funnel_yaw -= dx * k * 0.008;
        _s.funnel_pitch = std::min(1.45, std::max(-0.15, _s.funnel_pitch + dy * k * 0.006));
    } else {
        _s.azim_deg -= dx * k * 0.25;
        _s.incl_deg = std::min(177.0, std::max(3.0, _s.incl_deg - dy * k * 0.25));
    }
}

- (void)scrollWheel:(NSEvent*)e
{
    const double d = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY * 0.4 : e.scrollingDeltaY * 4.0;
    NSPoint p = [self backingPoint:e];
    if (_s.funnel_mode == 2 || _renderer->in_inset(p.x, p.y)) _s.funnel_dist = std::min(200.0, std::max(25.0, _s.funnel_dist * std::exp(-d * 0.006)));
    else _s.dist = std::min(1000.0, std::max(2.4, _s.dist * std::exp(-d * 0.006)));
}

- (void)magnifyWithEvent:(NSEvent*)e { _s.dist = std::min(1000.0, std::max(2.4, _s.dist * std::exp(-e.magnification * 1.5))); }

- (void)keyDown:(NSEvent*)e
{
    NSString* c = e.charactersIgnoringModifiers;
    if (c.length == 0) return;
    const unichar ch = [c characterAtIndex:0];
    auto onoff = [&](const char* name, bool v) { [self status:[NSString stringWithFormat:@"%s: %s", name, v ? "on" : "off"]]; };
    switch (ch) {
        case 27:
        case 'q': [NSApp terminate:nil]; break;
        case ' ': _s.paused = !_s.paused; [self status:_s.paused ? @"paused" : @"running"]; break;
        case 'd': _s.disk = !_s.disk; onoff("accretion disk", _s.disk); break;
        case 's': _s.stars = !_s.stars; onoff("stars", _s.stars); break;
        case 'g': _s.celestial_grid = !_s.celestial_grid; onoff("lensed sky grid", _s.celestial_grid); break;
        case 'b': _s.bloom = !_s.bloom; onoff("bloom", _s.bloom); break;
        case 't': _s.taa = !_s.taa; onoff("temporal anti-aliasing", _s.taa); break;
        case 'e': _s.turbulence = !_s.turbulence; onoff("disk turbulence", _s.turbulence); break;
        case 'h': _s.hud = !_s.hud; break;
        case 'o': _s.auto_orbit = !_s.auto_orbit; onoff("auto-orbit", _s.auto_orbit); break;
        case 'v': _s.halo = !_s.halo; onoff("hot corona halo", _s.halo); break;
        case 'c': _s.critical_curve = !_s.critical_curve; onoff("analytic shadow curve (Bardeen 1973)", _s.critical_curve); break;
        case 'f':
            _s.funnel_mode = (_s.funnel_mode + 1) % 3;
            [self status:(_s.funnel_mode == 0 ? @"funnel: off" : _s.funnel_mode == 1 ? @"funnel: inset" : @"funnel: fullscreen")];
            break;
        case 'r': {
            SimState d;
            d.hud = _s.hud; d.funnel_mode = _s.funnel_mode; d.sim_time = _s.sim_time; d.ui_scale = _s.ui_scale;
            _s = d;
            [self status:@"view reset"];
            break;
        }
        case 'p': {
            _opt.window_shot = [[NSString stringWithFormat:@"%@/Desktop/blackhole-%ld.png", NSHomeDirectory(), (long)time(nullptr)] UTF8String];
            _shotPending = false;
            [self status:@"screenshot saved to Desktop"];
            break;
        }
        case '[': _s.spin = std::max(0.0, _s.spin - 0.02); [self status:[NSString stringWithFormat:@"spin a = %.3f", _s.spin]]; break;
        case ']': _s.spin = std::min(0.998, _s.spin + 0.02); [self status:[NSString stringWithFormat:@"spin a = %.3f", _s.spin]]; break;
        case '-': _s.dist = std::min(1000.0, _s.dist * 1.1); break;
        case '=':
        case '+': _s.dist = std::max(2.4, _s.dist / 1.1); break;
        case ',': _s.t_peak = std::max(1500.0, _s.t_peak / 1.1); [self status:[NSString stringWithFormat:@"T_peak = %.0f K", _s.t_peak]]; break;
        case '.': _s.t_peak = std::min(60000.0, _s.t_peak * 1.1); [self status:[NSString stringWithFormat:@"T_peak = %.0f K", _s.t_peak]]; break;
        case ';': _s.exposure /= 1.15; [self status:[NSString stringWithFormat:@"exposure x%.2f", _s.exposure]]; break;
        case '\'': _s.exposure *= 1.15; [self status:[NSString stringWithFormat:@"exposure x%.2f", _s.exposure]]; break;
        case 'n': _s.time_scale /= 1.25; [self status:[NSString stringWithFormat:@"time x%.2f", _s.time_scale]]; break;
        case 'm': _s.time_scale *= 1.25; [self status:[NSString stringWithFormat:@"time x%.2f", _s.time_scale]]; break;
        case 'z': _s.fov_deg = std::max(10.0, _s.fov_deg - 3.0); break;
        case 'x': _s.fov_deg = std::min(110.0, _s.fov_deg + 3.0); break;
        case '0': case '1': case '2': case '3':
            _s.debug_view = ch - '0';
            [self status:(_s.debug_view == 0 ? @"view: normal" : _s.debug_view == 1 ? @"view: image order" : _s.debug_view == 2 ? @"view: redshift" : @"view: integrator cost")];
            break;
        case '8': _s.auto_scale = false; _s.render_scale = std::max(0.25, _s.render_scale / 1.15); [self status:[NSString stringWithFormat:@"render scale %.2f", _s.render_scale]]; break;
        case '9': _s.auto_scale = false; _s.render_scale = std::min(1.0, _s.render_scale * 1.15); [self status:[NSString stringWithFormat:@"render scale %.2f", _s.render_scale]]; break;
        case '7': _s.auto_scale = true; [self status:@"dynamic resolution: on"]; break;
        default: break;
    }
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic, strong) NSWindow* window;
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }
@end

static int run_window(Options& o, id<MTLDevice> dev)
{
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    AppDelegate* del = [AppDelegate new];
    app.delegate = del;

    // minimal menu so Cmd+Q / Cmd+W work
    NSMenu* bar = [NSMenu new];
    NSMenuItem* appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"Quit Kerr Black Hole" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    app.mainMenu = bar;

    NSRect frame = NSMakeRect(0, 0, 1440, 900);
    NSScreen* sc = [NSScreen mainScreen];
    if (sc) {
        frame.size.width = std::min(1500.0, sc.visibleFrame.size.width * 0.86);
        frame.size.height = std::min(940.0, sc.visibleFrame.size.height * 0.88);
    }
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.title = @"Kerr Black Hole — real-time geodesic ray tracing (Metal)";
    win.backgroundColor = [NSColor blackColor];
    BHView* view = [[BHView alloc] initWithFrame:frame device:dev options:o];
    if (!view) return 1;
    win.contentView = view;
    win.delegate = del;
    del.window = win;
    [win center];
    win.minSize = NSMakeSize(640, 400);
    [win makeKeyAndOrderFront:nil];
    [win makeFirstResponder:view];
    if (o.fullscreen) [win toggleFullScreen:nil];
    [app activateIgnoringOtherApps:YES];
    [app run];
    return 0;
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        Options o;
        if (!parse_args(argc, argv, o)) { usage(); return 2; }

        if (o.selftest) {
            int rc = run_selftest(true);
            id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
            if (dev) {
                Renderer R;
                std::string err;
                if (!R.init(dev, &err)) { std::fprintf(stderr, "renderer init failed: %s\n", err.c_str()); return 1; }
                rc += run_gpu_parity(dev, R);
            } else {
                std::puts("(no Metal device: skipped GPU parity)");
            }
            std::puts(rc == 0 ? "\nALL CHECKS PASSED" : "\nSOME CHECKS FAILED");
            return rc == 0 ? 0 : 1;
        }

        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { std::fprintf(stderr, "No Metal device found.\n"); return 1; }

        if (o.gputest) {
            Renderer R;
            std::string err;
            if (!R.init(dev, &err)) { std::fprintf(stderr, "renderer init failed: %s\n", err.c_str()); return 1; }
            return run_gpu_parity(dev, R) == 0 ? 0 : 1;
        }
        if (!o.shot.empty() || o.bench) {
            [NSApplication sharedApplication];
            Renderer R;
            std::string err;
            if (!R.init(dev, &err)) { std::fprintf(stderr, "renderer init failed: %s\n", err.c_str()); return 1; }
            return run_headless(o, dev, R);
        }
        return run_window(o, dev);
    }
}
