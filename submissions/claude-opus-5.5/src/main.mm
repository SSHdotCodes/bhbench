// main.mm — window, input, HUD and frame loop; plus headless modes:
//   bh                      interactive real-time window
//   bh --render out.png     offline still (progressive supersampling), see --help for options
//   bh --bench              GPU timing of the trace pass
//   bh --selftest           GPU-vs-CPU agreement of the geodesic core + cube-map convention check
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <mach-o/dyld.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "kerr_cpu.h"
#include "renderer.h"
#include "scene.h"

static std::string executableDir() {
    char buf[4096];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return ".";
    std::string p = [[@(buf) stringByResolvingSymlinksInPath] UTF8String];
    return p.substr(0, p.find_last_of('/'));
}

static std::string findMetallib() {
    std::string d = executableDir();
    for (std::string c : {d + "/bh.metallib", d + "/../Resources/bh.metallib"})
        if ([[NSFileManager defaultManager] fileExistsAtPath:@(c.c_str())]) return c;
    return d + "/bh.metallib";
}

static simd_float2 r2Jitter(int n) {   // R2 low-discrepancy sequence in [-0.5, 0.5)²
    if (n <= 0) return simd_make_float2(0, 0);
    double x = std::fmod(0.5 + n * 0.7548776662466927, 1.0), y = std::fmod(0.5 + n * 0.5698402909980532, 1.0);
    return simd_make_float2(float(x - 0.5), float(y - 0.5));
}

static PostUniforms makePost(const AppState& s, int w, int h, uint64_t frame) {
    PostUniforms P;
    memset(&P, 0, sizeof P);
    P.outSize = simd_make_float2(float(w), float(h));
    P.bloomStrength = 0.06f;
    P.bloomNorm = 1.0f / 6.0f;
    P.stretch = float(s.stretch);
    P.frameIndex = float(frame % 100000);
    P.tonemap = s.tonemap;
    P.bloomOn = s.bloom ? 1 : 0;
    return P;
}

// ============================================================================================
// Interactive application
// ============================================================================================
struct App {
    AppState S, defaults;
    SceneCache scene;
    Renderer R;
    CameraInfo info;
    double tR = 0, tTheta = 0, tPhi = 0, tYaw = 0, tPitch = 0, tFov = 0;   // smoothing targets
    double renderScale = 0.7, fps = 60, lastT = 0, hudT = 0;
    std::atomic<double> gpuMs{0.0};
    FrameUniforms lastSig;
    bool haveSig = false;
    int accum = 0, staticFrames = 0;
    const int maxAccum = 256;
    uint64_t frame = 0;
    NSTextField* hud = nil;
    NSTextField* help = nil;
    int hudMode = 0;   // 0 HUD, 1 HUD + help, 2 nothing
    int lastW = 0, lastH = 0;
    std::string screenshotDir;

    void syncTargets() { tR = S.camR; tTheta = S.camTheta; tPhi = S.camPhi; tYaw = S.yaw; tPitch = S.pitch; tFov = S.fovDeg; }

    void preset(int k) {
        S.yaw = S.pitch = 0;
        S.plunging = false;
        switch (k) {
            case 1: S.camR = 40; S.camTheta = 83; S.camPhi = 0; S.fovDeg = 40; S.gridMode = BH_GRID_OFF; break;
            case 2: S.camR = 32; S.camTheta = 89.3; S.camPhi = 0; S.fovDeg = 42; break;
            case 3: S.camR = 30; S.camTheta = 18; S.camPhi = 0; S.fovDeg = 45; break;
            case 4: S.camR = 58; S.camTheta = 62; S.camPhi = 25; S.fovDeg = 52; S.gridMode = BH_GRID_FUNNEL; break;
            case 5: S.camR = 9; S.camTheta = 80; S.camPhi = 0; S.fovDeg = 80; break;
            case 6: S.camR = 26; S.camTheta = 98; S.camPhi = 0; S.fovDeg = 50; break;
        }
        syncTargets();
    }

    void update(double dt) {
        double a = S.spin;
        double rmin = phys::horizon(a) + 0.08;
        if (!S.paused) S.simTime += dt * S.timeScale;
        if (S.autoOrbit && !S.plunging) tPhi += dt * 5.0;
        if (S.plunging) {
            // radial free fall from rest at infinity (E = 1, L = 0): dr/dτ = −sqrt(2r(r²+a²))/r², dφ/dτ = 2a/(rΔ)
            double dtau = dt * std::max(1.0, S.timeScale * 0.4);
            int sub = 32;
            for (int i = 0; i < sub && S.camR > rmin; ++i) {
                double r = S.camR, h = dtau / sub;
                double delta = r * r - 2 * r + a * a;
                S.camR -= h * std::sqrt(2 * r * (r * r + a * a)) / (r * r);
                S.camPhi += h * 2 * a / (r * delta) * 180 / M_PI;
            }
            if (S.camR <= rmin) { S.camR = rmin; S.plunging = false; }
            tR = S.camR;
            tPhi = S.camPhi;
        }
        tR = std::clamp(tR, rmin, 3000.0);
        tTheta = std::clamp(tTheta, 0.5, 179.5);
        tPitch = std::clamp(tPitch, -85.0, 85.0);
        tFov = std::clamp(tFov, 5.0, 150.0);
        double k = 1.0 - std::exp(-dt * 14.0);
        auto approach = [&](double& v, double t, double eps) {
            v += (t - v) * k;
            if (std::fabs(t - v) < eps) v = t;
        };
        approach(S.camR, tR, 1e-5 * tR);
        approach(S.camTheta, tTheta, 1e-4);
        approach(S.camPhi, tPhi, 1e-4);
        approach(S.yaw, tYaw, 1e-4);
        approach(S.pitch, tPitch, 1e-4);
        approach(S.fovDeg, tFov, 1e-4);
    }

    void draw(MTKView* view) {
        double now = CACurrentMediaTime();
        double dt = lastT > 0 ? std::clamp(now - lastT, 0.0, 0.1) : 1.0 / 60;
        lastT = now;
        fps = 0.9 * fps + 0.1 / std::max(dt, 1e-4);
        update(dt);
        updateScene(S, scene);
        R.syncTables(scene);

        int W = int(view.drawableSize.width), H = int(view.drawableSize.height);
        if (W < 8 || H < 8) return;

        // Is the picture static?  (identical uniforms ⇒ progressive supersampling at full resolution)
        AppState sigState = S;
        if (!S.turbulence) sigState.simTime = 0;   // the image does not depend on time then
        FrameUniforms sig = buildUniforms(sigState, scene, W, H, simd_make_float2(0, 0));
        bool same = haveSig && memcmp(&sig, &lastSig, sizeof sig) == 0;
        lastSig = sig;
        haveSig = true;
        staticFrames = same ? staticFrames + 1 : 0;
        bool still = staticFrames >= 2;
        if (!still) accum = 0;

        double scale = still ? 1.0 : std::round(renderScale * 20.0) / 20.0;
        int tw = std::max(8, int(std::lround(W * scale))), th = std::max(8, int(std::lround(H * scale)));
        if (tw != R.traceWidth() || th != R.traceHeight()) {
            R.ensureTraceSize(tw, th);
            accum = 0;
        }
        bool doTrace = accum < maxAccum;
        simd_float2 jit = still ? r2Jitter(accum) : simd_make_float2(0, 0);
        FrameUniforms U = buildUniforms(sigState, scene, tw, th, jit, &info);
        float weight = 1.0f / float(accum + 1);
        if (doTrace) ++accum;

        id<CAMetalDrawable> drawable = view.currentDrawable;
        if (!drawable) return;
        id<MTLCommandBuffer> cb = [R.queue() commandBuffer];
        R.encodeFrame(cb, U, doTrace, weight, makePost(S, W, H, frame++), drawable.texture);
        [cb presentDrawable:drawable];
        std::atomic<double>* gm = &gpuMs;
        bool traced = doTrace;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
            if (traced) gm->store(1000.0 * (b.GPUEndTime - b.GPUStartTime));
        }];
        [cb commit];

        // dynamic resolution while the view is changing: aim at ~60 fps with headroom
        if (!still && doTrace) {
            double g = gpuMs.load();
            if (g > 0.5) {
                double target = 14.0;   // ms of GPU time per frame (60 fps with headroom)
                double sNew = std::clamp(renderScale * std::sqrt(target / g), 0.3, 1.0);
                renderScale = 0.85 * renderScale + 0.15 * sNew;
            }
        }
        lastW = tw;
        lastH = th;
        if (now - hudT > 0.2) {
            hudT = now;
            refreshHUD();
        }
    }

    void refreshHUD() {
        if (!hud) return;
        hud.hidden = hudMode == 2;
        help.hidden = hudMode != 1;
        if (hudMode == 2) return;
        std::string t = hudText(S, scene, info, fps, gpuMs.load(), lastW, lastH, accum);
        hud.stringValue = @(t.c_str());
        [hud sizeToFit];
        NSRect f = hud.frame;
        f.origin = NSMakePoint(12, hud.superview.bounds.size.height - f.size.height - 12);
        hud.frame = f;
        [help sizeToFit];
        NSRect hf = help.frame;
        hf.origin = NSMakePoint(12, 12);
        help.frame = hf;
    }

    void screenshot(MTKView* view) {
        int W = int(view.drawableSize.width), H = int(view.drawableSize.height);
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:W height:H mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex = [R.device() newTextureWithDescriptor:td];
        id<MTLBuffer> buf = [R.device() newBufferWithLength:size_t(W) * H * 4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = [R.queue() commandBuffer];
        FrameUniforms U = buildUniforms(S, scene, R.traceWidth(), R.traceHeight(), simd_make_float2(0, 0));
        R.encodeFrame(cb, U, false, 0, makePost(S, W, H, frame), tex);
        id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
        [be copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:MTLSizeMake(W, H, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:size_t(W) * 4
        destinationBytesPerImage:size_t(W) * H * 4];
        [be endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        [[NSFileManager defaultManager] createDirectoryAtPath:@(screenshotDir.c_str()) withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        NSDateFormatter* df = [NSDateFormatter new];
        df.dateFormat = @"yyyyMMdd-HHmmss";
        std::string path = screenshotDir + "/blackhole-" + [[df stringFromDate:[NSDate date]] UTF8String] + ".png";
        bool ok = writePNG(path, (const uint8_t*)buf.contents, W, H, W * 4);
        std::printf("%s %s\n", ok ? "saved" : "FAILED to save", path.c_str());
    }

    void key(NSEvent* e, MTKView* view) {
        NSString* ch = e.charactersIgnoringModifiers;
        unichar c = ch.length ? [ch characterAtIndex:0] : 0;
        bool shift = (e.modifierFlags & NSEventModifierFlagShift) != 0;
        switch (c) {
            case ' ': S.paused = !S.paused; break;
            case '[': S.spin = std::max(-0.998, std::round((S.spin - 0.05) * 100) / 100); break;
            case ']': S.spin = std::min(0.998, std::round((S.spin + 0.05) * 100) / 100); break;
            case '{': S.spin = std::max(-0.998, S.spin - 0.01); break;
            case '}': S.spin = std::min(0.998, S.spin + 0.01); break;
            case 'g': case 'G': S.gridMode = (S.gridMode + 1) % 3; break;
            case 'l': case 'L': S.gr = !S.gr; break;
            case 'z': case 'Z': S.redshift = !S.redshift; break;
            case 'd': case 'D': S.disk = !S.disk; break;
            case 't': case 'T': S.turbulence = !S.turbulence; break;
            case 'k': case 'K': S.limb = !S.limb; break;
            case 'y': case 'Y': S.sky = !S.sky; break;
            case 'b': case 'B': S.bloom = !S.bloom; break;
            case 'o': case 'O': S.orderViz = !S.orderViz; break;
            case 'c': case 'C': S.shadowOverlay = !S.shadowOverlay; break;
            case 'v': case 'V': S.motion = (S.motion + 1) % 3; S.plunging = false; break;
            case 'f': case 'F':
                S.plunging = !S.plunging;
                if (S.plunging) S.motion = MOTION_INFALL;
                break;
            case 'a': case 'A': S.autoOrbit = !S.autoOrbit; break;
            case 'm': case 'M': S.tonemap = (S.tonemap + 1) % 3; break;
            case '-': case '_': S.exposureEV -= 0.25; break;
            case '=': case '+': S.exposureEV += 0.25; break;
            case ',': case '<': tFov /= 1.1; break;
            case '.': case '>': tFov *= 1.1; break;
            case 'h': case 'H': hudMode = (hudMode + 1) % 3; refreshHUD(); break;
            case 's': case 'S': screenshot(view); break;
            case 'x': case 'X': {
                int hm = hudMode;
                S = defaults;
                syncTargets();
                hudMode = hm;
                break;
            }
            case 'q': case 'Q': case 27: [NSApp terminate:nil]; break;
            case '1': case '2': case '3': case '4': case '5': case '6': preset(c - '0'); break;
            case NSUpArrowFunctionKey: S.Tpeak = std::min(2e7, S.Tpeak * (shift ? 1.05 : 1.25)); break;
            case NSDownArrowFunctionKey: S.Tpeak = std::max(1500.0, S.Tpeak / (shift ? 1.05 : 1.25)); break;
            case NSRightArrowFunctionKey: S.timeScale = std::min(1000.0, S.timeScale * 2); break;
            case NSLeftArrowFunctionKey: S.timeScale = std::max(0.125, S.timeScale / 2); break;
            default: break;
        }
        refreshHUD();
    }
};

// ============================================================================================
// Cocoa glue
// ============================================================================================
@interface BHView : MTKView
@property(nonatomic, assign) App* app;
@end

@implementation BHView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (void)keyDown:(NSEvent*)e { self.app->key(e, self); }
- (void)orbit:(NSEvent*)e {
    App* a = self.app;
    double k = 0.25 * std::clamp(a->S.fovDeg / 50.0, 0.2, 2.0);
    a->tPhi -= e.deltaX * k;
    a->tTheta -= e.deltaY * k;
}
- (void)look:(NSEvent*)e {
    App* a = self.app;
    double k = 0.12 * a->S.fovDeg / 50.0;
    a->tYaw -= e.deltaX * k;
    a->tPitch += e.deltaY * k;
}
- (void)mouseDragged:(NSEvent*)e {
    if (e.modifierFlags & NSEventModifierFlagOption) [self look:e]; else [self orbit:e];
}
- (void)rightMouseDragged:(NSEvent*)e { [self look:e]; }
- (void)otherMouseDragged:(NSEvent*)e { [self look:e]; }
- (void)scrollWheel:(NSEvent*)e {
    App* a = self.app;
    double d = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY * 0.004 : e.scrollingDeltaY * 0.08;
    a->tR *= std::exp(-d);
    a->S.plunging = false;
}
- (void)magnifyWithEvent:(NSEvent*)e {
    self.app->tR *= std::exp(-1.5 * e.magnification);
    self.app->S.plunging = false;
}
@end

@interface BHDelegate : NSObject <MTKViewDelegate, NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, assign) App* app;
@end

@implementation BHDelegate
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size { (void)view; (void)size; }
- (void)drawInMTKView:(MTKView*)view { self.app->draw(view); }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)s { (void)s; return YES; }
@end

static NSTextField* makeLabel(NSView* parent) {
    NSTextField* t = [NSTextField labelWithString:@""];
    t.font = [NSFont monospacedSystemFontOfSize:11.5 weight:NSFontWeightRegular];
    t.textColor = [NSColor colorWithWhite:0.93 alpha:1.0];
    t.drawsBackground = YES;
    t.backgroundColor = [NSColor colorWithWhite:0.0 alpha:0.5];
    t.wantsLayer = YES;
    t.layer.cornerRadius = 6;
    t.bezeled = NO;
    t.editable = NO;
    t.selectable = NO;
    [parent addSubview:t];
    return t;
}

static int runInteractive(App& app) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSMenu* bar = [NSMenu new];
        NSMenuItem* appItem = [NSMenuItem new];
        [bar addItem:appItem];
        NSMenu* appMenu = [NSMenu new];
        [appMenu addItemWithTitle:@"Quit Black Hole" action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        NSApp.mainMenu = bar;

        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            std::fprintf(stderr, "no Metal device\n");
            return 1;
        }
        std::string err;
        if (!app.R.init(dev, findMetallib(), MTLPixelFormatBGRA8Unorm, err)) {
            std::fprintf(stderr, "renderer init failed: %s\n", err.c_str());
            return 1;
        }
        updateScene(app.S, app.scene);
        app.R.syncTables(app.scene);
        double t0 = CACurrentMediaTime();
        app.R.generateSky(2048);
        std::printf("Metal device: %s   sky generated in %.0f ms\n", dev.name.UTF8String, 1000 * (CACurrentMediaTime() - t0));

        NSRect screen = NSScreen.mainScreen.visibleFrame;
        CGFloat w = std::min<CGFloat>(1600, screen.size.width * 0.9), h = std::min<CGFloat>(1000, screen.size.height * 0.9);
        NSRect frame = NSMakeRect(screen.origin.x + (screen.size.width - w) / 2, screen.origin.y + (screen.size.height - h) / 2, w, h);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                      backing:NSBackingStoreBuffered defer:NO];
        win.title = @"Kerr Black Hole — general-relativistic ray tracer (Metal)";
        BHView* view = [[BHView alloc] initWithFrame:win.contentView.bounds device:dev];
        view.app = &app;
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.preferredFramesPerSecond = 60;   // steady 60 fps; the dynamic resolution uses the budget
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        ((CAMetalLayer*)view.layer).colorspace = srgb;
        CGColorSpaceRelease(srgb);
        BHDelegate* del = [BHDelegate new];
        del.app = &app;
        view.delegate = del;
        NSApp.delegate = del;
        win.contentView = view;
        app.hud = makeLabel(view);
        app.help = makeLabel(view);
        app.help.stringValue = @(helpText().c_str());
        app.hud.autoresizingMask = NSViewMinYMargin;
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];
        [NSApp activateIgnoringOtherApps:YES];
        app.refreshHUD();
        [NSApp run];
    }
    return 0;
}

// ============================================================================================
// Headless modes
// ============================================================================================
static bool argVal(int argc, char** argv, const char* name, std::string& out) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == name) { out = argv[i + 1]; return true; }
    return false;
}
static bool argFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == name) return true;
    return false;
}

static void applyArgs(int argc, char** argv, AppState& S) {
    std::string v;
    if (argVal(argc, argv, "--spin", v)) S.spin = std::stod(v);
    if (argVal(argc, argv, "--r", v)) S.camR = std::stod(v);
    if (argVal(argc, argv, "--incl", v)) S.camTheta = std::stod(v);
    if (argVal(argc, argv, "--phi", v)) S.camPhi = std::stod(v);
    if (argVal(argc, argv, "--fov", v)) S.fovDeg = std::stod(v);
    if (argVal(argc, argv, "--yaw", v)) S.yaw = std::stod(v);
    if (argVal(argc, argv, "--pitch", v)) S.pitch = std::stod(v);
    if (argVal(argc, argv, "--grid", v)) S.gridMode = std::stoi(v);
    if (argVal(argc, argv, "--time", v)) S.simTime = std::stod(v);
    if (argVal(argc, argv, "--ev", v)) S.exposureEV = std::stod(v);
    if (argVal(argc, argv, "--tpeak", v)) S.Tpeak = std::stod(v);
    if (argVal(argc, argv, "--motion", v)) S.motion = std::stoi(v);
    if (argVal(argc, argv, "--tonemap", v)) S.tonemap = std::stoi(v);
    if (argVal(argc, argv, "--stretch", v)) S.stretch = std::stod(v);
    if (argVal(argc, argv, "--rout", v)) S.rDiskOut = std::stod(v);
    if (argVal(argc, argv, "--turb", v)) S.turbAmp = std::stod(v);
    if (argVal(argc, argv, "--steps", v)) S.stepScale = std::stod(v);
    if (argFlag(argc, argv, "--flat")) S.gr = false;
    if (argFlag(argc, argv, "--no-redshift")) S.redshift = false;
    if (argFlag(argc, argv, "--no-turb")) S.turbulence = false;
    if (argFlag(argc, argv, "--no-disk")) S.disk = false;
    if (argFlag(argc, argv, "--no-sky")) S.sky = false;
    if (argFlag(argc, argv, "--no-bloom")) S.bloom = false;
    if (argFlag(argc, argv, "--no-limb")) S.limb = false;
    if (argFlag(argc, argv, "--order")) S.orderViz = true;
    if (argFlag(argc, argv, "--shadow")) S.shadowOverlay = true;
}

static bool initHeadless(Renderer& R, AppState& S, SceneCache& scene, int skySize) {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    std::string err;
    if (!dev || !R.init(dev, findMetallib(), MTLPixelFormatBGRA8Unorm, err)) {
        std::fprintf(stderr, "renderer init failed: %s\n", err.c_str());
        return false;
    }
    updateScene(S, scene);
    R.syncTables(scene);
    R.generateSky(skySize);
    return true;
}

static int runRender(int argc, char** argv, const std::string& out) {
    AppState S;
    applyArgs(argc, argv, S);
    int W = 1920, H = 1080, spp = 64;
    std::string v;
    if (argVal(argc, argv, "--size", v)) std::sscanf(v.c_str(), "%dx%d", &W, &H);
    if (argVal(argc, argv, "--spp", v)) spp = std::max(1, std::stoi(v));
    Renderer R;
    SceneCache scene;
    if (!initHeadless(R, S, scene, 2048)) return 1;
    R.ensureTraceSize(W, H);
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:W height:H mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [R.device() newTextureWithDescriptor:td];
    id<MTLBuffer> buf = [R.device() newBufferWithLength:size_t(W) * H * 4 options:MTLResourceStorageModeShared];
    double t0 = CACurrentMediaTime();
    for (int i = 0; i < spp; ++i) {
        FrameUniforms U = buildUniforms(S, scene, W, H, r2Jitter(i));
        id<MTLCommandBuffer> cb = [R.queue() commandBuffer];
        R.encodeFrame(cb, U, true, 1.0f / float(i + 1), makePost(S, W, H, 0), tex);
        if (i == spp - 1) {
            id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
            [be copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                     sourceSize:MTLSizeMake(W, H, 1) toBuffer:buf destinationOffset:0
         destinationBytesPerRow:size_t(W) * 4 destinationBytesPerImage:size_t(W) * H * 4];
            [be endEncoding];
        }
        [cb commit];
        if (i == spp - 1) [cb waitUntilCompleted];
    }
    double dt = CACurrentMediaTime() - t0;
    bool ok = writePNG(out, (const uint8_t*)buf.contents, W, H, W * 4);
    std::printf("%s %s  (%dx%d, %d spp, %.1f ms/spp)\n", ok ? "wrote" : "FAILED", out.c_str(), W, H, spp, 1000 * dt / spp);
    return ok ? 0 : 1;
}

static int runBench(int argc, char** argv) {
    AppState S;
    applyArgs(argc, argv, S);
    int W = 3200, H = 2000, n = 40;
    std::string v;
    if (argVal(argc, argv, "--size", v)) std::sscanf(v.c_str(), "%dx%d", &W, &H);
    Renderer R;
    SceneCache scene;
    if (!initHeadless(R, S, scene, 2048)) return 1;
    R.ensureTraceSize(W, H);
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:W height:H mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [R.device() newTextureWithDescriptor:td];
    std::vector<double> ms;
    for (int i = 0; i < n; ++i) {
        S.simTime = i * 0.5;
        FrameUniforms U = buildUniforms(S, scene, W, H, simd_make_float2(0, 0));
        id<MTLCommandBuffer> cb = [R.queue() commandBuffer];
        R.encodeFrame(cb, U, true, 1.0f, makePost(S, W, H, i), tex);
        [cb commit];
        [cb waitUntilCompleted];
        if (i >= 5) ms.push_back(1000.0 * (cb.GPUEndTime - cb.GPUStartTime));
    }
    std::sort(ms.begin(), ms.end());
    std::printf("%dx%d (%.1f Mpix): GPU frame time median %.2f ms (min %.2f, max %.2f) → %.0f fps\n", W, H,
                W * H / 1e6, ms[ms.size() / 2], ms.front(), ms.back(), 1000.0 / ms[ms.size() / 2]);
    return 0;
}

// CPU replica of the shader's disk visitor (for the GPU-vs-CPU comparison): same opacity model, including the
// taper of the outer 30% of the disk, so rays stop exactly where the shader stops them.
struct DebugVisitor {
    float rin, rout, aDisk, opacity;
    bool disk, gr;
    float firstR = -1, firstG = 0, trans = 1;
    float funnelHeight(float) const { return 0; }
    static float smoothstep(float e0, float e1, float x) {
        float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    bool onPlane(const kerr32::KerrRay& k, const kerr32::RayState& c, int, float, bool) {
        float r = 1.0f / c.rho;
        if (disk && r >= rin && r <= rout) {
            float g = 1;
            if (gr) g = 1.0f / (kerr32::kerr_orbit_ut(aDisk, r) * std::max(k.E - kerr32::kerr_orbit_omega(aDisk, r) * k.L, 1e-6f));
            if (firstR < 0) { firstR = r; firstG = g; }
            trans *= 1.0f - opacity * (1.0f - smoothstep(0.7f * rout, rout, r));
        }
        return trans < 0.002f;
    }
    bool onFunnel(const kerr32::KerrRay&, const kerr32::RayState&, const kerr32::RayState&, const kerr32::RayState&, float) {
        return false;
    }
};

static int runSelfTest() {
    AppState S;
    S.turbulence = false;
    Renderer R;
    SceneCache scene;
    if (!initHeadless(R, S, scene, 256)) return 1;
    int failures = 0;
    std::string rep;
    bool cubeOK = R.cubeConventionTest(rep);
    std::printf("%s %s\n", cubeOK ? "PASS" : "FAIL", rep.c_str());
    failures += !cubeOK;

    struct Cfg { const char* name; double spin, r, incl, fov; int motion; bool gr; };
    const Cfg cfgs[] = {
        {"a=0.90 classic view", 0.90, 28, 82, 50, MOTION_HOVER, true},
        {"a=0.998 edge-on", 0.998, 32, 89.3, 40, MOTION_HOVER, true},
        {"a=0 face-on", 0.0, 30, 18, 45, MOTION_HOVER, true},
        {"a=-0.6 close-up", -0.6, 9, 80, 80, MOTION_HOVER, true},
        {"a=0.9 orbiting observer", 0.9, 12, 86, 70, MOTION_ORBIT, true},
        {"a=0.9 infalling observer", 0.9, 5, 70, 90, MOTION_INFALL, true},
        {"flat space", 0.9, 28, 82, 50, MOTION_HOVER, false},
    };
    const int W = 160, H = 100;
    for (const Cfg& c : cfgs) {
        S.spin = c.spin; S.camR = c.r; S.camTheta = c.incl; S.fovDeg = c.fov; S.motion = c.motion; S.gr = c.gr;
        updateScene(S, scene);
        R.syncTables(scene);
        FrameUniforms U = buildUniforms(S, scene, W, H, simd_make_float2(0, 0));
        std::vector<simd_float4> gpu = R.traceDebug(U, W, H);
        kerr32::CamFrame cam;
        cam.right = V3<float>(U.camRight.x, U.camRight.y, U.camRight.z);
        cam.up = V3<float>(U.camUp.x, U.camUp.y, U.camUp.z);
        cam.fwd = V3<float>(U.camFwd.x, U.camFwd.y, U.camFwd.z);
        cam.beta = V3<float>(U.camBeta.x, U.camBeta.y, U.camBeta.z);
        cam.gamma = U.camGamma; cam.r = U.camR; cam.sinT = U.camSinT; cam.cosT = U.camCosT;
        cam.sinP = U.camSinP; cam.cosP = U.camCosP; cam.Sigma = U.camSigma; cam.Delta = U.camDelta;
        cam.Akerr = U.camA; cam.alpha = U.camAlpha; cam.omega = U.camOmega; cam.varpi = U.camVarpi;
        kerr32::TraceCfg cfg;
        cfg.rhoHorizon = U.rhoHorizon; cfg.rhoCapture = U.rhoCapture; cfg.rhoEscape = 0;
        cfg.invStepAng = 1.0f / U.stepAng; cfg.invStepRho = 1.0f / U.stepRho; cfg.invStepPhi = 1.0f / U.stepPhi;
        cfg.funnelRho = 0; cfg.maxSteps = U.maxSteps; cfg.wantTime = 0;
        int statusMis = 0, valueMis = 0, n = 0;
        double worstDir = 0, worstR = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float nx = px / W * 2 - 1, ny = 1 - py / H * 2;
                V3<float> dl(nx * U.tanHalfFov * U.aspect, ny * U.tanHalfFov, 1.0f);
                kerr32::KerrRay k;
                kerr32::RayState s;
                kerr32::kerr_init_ray(cam, U.spin, U.mass, dl, k, s);
                DebugVisitor v{U.diskRin, U.rDiskOut, U.diskSpin, U.diskOpacity, true, c.gr};
                kerr32::TraceResult tr = kerr32::kerr_trace(k, s, cfg, v);
                simd_float4 g = gpu[size_t(y) * W + x];
                ++n;
                if (int(g.x) != tr.status) { ++statusMis; continue; }
                if (tr.status == BH_TRACE_ESCAPED) {
                    V3<double> a(g.y, g.z, g.w), b(tr.dir.x, tr.dir.y, tr.dir.z);
                    double ang = std::atan2(length(cross(a, b)), dot(a, b));
                    worstDir = std::max(worstDir, ang);
                    if (ang > 1e-3) ++valueMis;
                } else if (tr.status == BH_TRACE_ABSORBED) {
                    double e = std::fabs(g.y - v.firstR) / v.firstR + std::fabs(g.z - v.firstG);
                    worstR = std::max(worstR, e);
                    if (e > 1e-3) ++valueMis;
                }
            }
        double frac = double(statusMis + valueMis) / n;
        bool ok = frac < 0.003;
        failures += !ok;
        std::printf("%s GPU vs CPU core [%s]: %d px, %d status + %d value mismatches (%.3f%%), worst sky-dir %.1e rad, "
                    "worst disk-hit %.1e\n", ok ? "PASS" : "FAIL", c.name, n, statusMis, valueMis, 100 * frac, worstDir, worstR);
    }
    std::printf("%s\n", failures ? "SELF-TEST FAILED" : "SELF-TEST PASSED");
    return failures ? 1 : 0;
}

// Exercises the view's input handlers with events delivered in-process (nothing is posted to the OS).
static int runInputTest() {
    static App app;
    app.syncTargets();
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    BHView* view = [[BHView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300) device:dev];
    view.app = &app;
    view.paused = YES;
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        fails += !ok;
    };
    auto drag = [&](CGEventType t, double dx, double dy, CGEventFlags flags) {
        CGEventRef e = CGEventCreateMouseEvent(nullptr, t, CGPointMake(200, 150),
                                               t == kCGEventRightMouseDragged ? kCGMouseButtonRight : kCGMouseButtonLeft);
        CGEventSetIntegerValueField(e, kCGMouseEventDeltaX, int64_t(dx));
        CGEventSetIntegerValueField(e, kCGMouseEventDeltaY, int64_t(dy));
        CGEventSetFlags(e, flags);
        NSEvent* ev = [NSEvent eventWithCGEvent:e];
        CFRelease(e);
        return ev;
    };
    double phi0 = app.tPhi, th0 = app.tTheta;
    [view mouseDragged:drag(kCGEventLeftMouseDragged, 40, 0, 0)];
    check(app.tPhi < phi0 - 1, "drag right orbits the camera (φ decreases)");
    [view mouseDragged:drag(kCGEventLeftMouseDragged, 0, 40, 0)];
    check(app.tTheta < th0 - 1, "drag down raises the camera (inclination decreases)");
    double yaw0 = app.tYaw, pitch0 = app.tPitch;
    [view rightMouseDragged:drag(kCGEventRightMouseDragged, 30, 20, 0)];
    check(app.tYaw < yaw0 && app.tPitch > pitch0, "right-drag looks around");
    yaw0 = app.tYaw;
    [view mouseDragged:drag(kCGEventLeftMouseDragged, 30, 0, kCGEventFlagMaskAlternate)];
    check(app.tYaw < yaw0, "option-drag looks around");
    double r0 = app.tR;
    CGEventRef se = CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitPixel, 1, 50, 0, 0);
    [view scrollWheel:[NSEvent eventWithCGEvent:se]];
    CFRelease(se);
    check(app.tR < r0, "scroll up zooms in");
    auto key = [&](NSString* c) {
        NSEvent* ev = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                                   windowNumber:0 context:nil characters:c charactersIgnoringModifiers:c isARepeat:NO keyCode:0];
        [view keyDown:ev];
    };
    key(@"g");
    check(app.S.gridMode == BH_GRID_FUNNEL, "G switches to the embedding funnel");
    key(@"g");
    check(app.S.gridMode == BH_GRID_PLANE, "G again switches to the equatorial grid");
    double a0 = app.S.spin;
    key(@"]");
    check(std::fabs(app.S.spin - std::min(0.998, a0 + 0.05)) < 1e-9, "] increases the spin");
    key(@"l");
    check(!app.S.gr, "L toggles lensing off");
    key(@"4");
    check(app.tR == 58 && app.S.gridMode == BH_GRID_FUNNEL, "preset 4 = spacetime-grid view");
    double T0 = app.S.Tpeak;
    unichar up = NSUpArrowFunctionKey;
    key([NSString stringWithCharacters:&up length:1]);
    check(app.S.Tpeak > T0, "up arrow raises the disk temperature");
    key(@"f");
    check(app.S.plunging && app.S.motion == MOTION_INFALL, "F starts the plunge");
    std::printf("%s\n", fails ? "INPUT TEST FAILED" : "INPUT TEST PASSED");
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        std::string out;
        if (argFlag(argc, argv, "--help")) {
            std::printf("usage: bh [--render out.png [--size WxH] [--spp N]] [--bench [--size WxH]] [--selftest]\n"
                        "  scene: --spin a --r R --incl deg --phi deg --fov deg --yaw deg --pitch deg --grid 0|1|2\n"
                        "         --time M --ev EV --tpeak K --motion 0|1|2 --tonemap 0|1|2 --stretch s --rout R\n"
                        "         --turb amp --steps scale --flat --no-redshift --no-turb --no-disk --no-sky\n"
                        "         --no-bloom --no-limb --order --shadow\n\n%s\n", helpText().c_str());
            return 0;
        }
        if (argFlag(argc, argv, "--selftest")) return runSelfTest();
        if (argFlag(argc, argv, "--inputtest")) return runInputTest();
        if (argFlag(argc, argv, "--bench")) return runBench(argc, argv);
        if (argVal(argc, argv, "--render", out)) return runRender(argc, argv, out);
        static App app;
        applyArgs(argc, argv, app.S);
        app.defaults = AppState();
        app.syncTargets();
        std::string ed = executableDir();
        bool bundled = ed.size() >= 14 && ed.compare(ed.size() - 14, 14, "Contents/MacOS") == 0;
        app.screenshotDir = bundled ? std::string(NSHomeDirectory().UTF8String) + "/Pictures/Black Hole"
                                    : ed + "/../screenshots";
        return runInteractive(app);
    }
}
