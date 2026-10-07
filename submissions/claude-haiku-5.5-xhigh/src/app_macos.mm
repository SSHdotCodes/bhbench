// macOS app: opens a window and renders the Kerr black hole with Metal in real time.
//
//   bin/blackhole                       interactive window (lensing view)
//   bin/blackhole --view sheet          start in the spacetime-sheet view
//   bin/blackhole --snapshot out.png    render one frame offscreen and exit
//
// Controls (window must be focused):
//   1 / 2       lensing view / spacetime sheet ("trapdoor")
//   [ / ]       decrease / increase spin a
//   b           cycle background: stars, grid, both
//   d           toggle the accretion disk
//   + / -       exposure
//   p           pause / resume the automatic camera orbit
//   r           reset the camera
//   drag        orbit the camera;  scroll or pinch: zoom
//   q           quit
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shared_types.h"
#include "kerr_core.h"
#include "shading.h"
#include "disk_model.h"
#include "funnel_model.h"
#include "scene.h"
#include "png_out.h"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif

namespace {

struct AppState {
    Scene scene;
    int view = 0;                 // 0 lensing, 1 spacetime sheet
    bool auto_orbit = true;
    bool quit_after_frames = false;
    long frames_to_run = 0;
    long frames_done = 0;
    double funnel_dist = 34.0;
    double render_scale = 1.0;
    bool adaptive_scale = true;
    double gpu_ms = 0.0;
    double last_title_update = 0.0;
    double last_frame_time = 0.0;
    DiskModel disk;
    FunnelProfile funnel;
    unsigned funnel_vertex_count = 0;
    double r_ph = 3.0;
};

AppState g_app;

const char* kBackgroundNames[] = {"stars", "grid", "stars + grid"};

// Column-major 4x4 helpers for the sheet's camera.
void mat_mul(const double a[16], const double b[16], double out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
    }
}

void look_at(const double eye[3], const double center[3], const double up[3], double m[16]) {
    double f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (double& v : f) v /= fl;
    double s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    double sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (double& v : s) v /= sl;
    double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    double d0 = s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2];
    double d1 = u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2];
    double d2 = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    m[0] = s[0];  m[4] = s[1];  m[8] = s[2];   m[12] = -d0;
    m[1] = u[0];  m[5] = u[1];  m[9] = u[2];   m[13] = -d1;
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] = d2;
    m[3] = 0;     m[7] = 0;     m[11] = 0;     m[15] = 1;
}

// Right-handed perspective with Metal's depth range [0, 1].
void perspective(double fovy_rad, double aspect, double n, double f, double m[16]) {
    double t = 1.0 / std::tan(0.5 * fovy_rad);
    for (int i = 0; i < 16; ++i) m[i] = 0.0;
    m[0] = t / aspect;
    m[5] = t;
    m[10] = f / (n - f);
    m[11] = -1.0;
    m[14] = n * f / (n - f);
}

FunnelUniforms make_funnel_uniforms(double aspect) {
    double center[3] = {0.0, 0.5 * (g_app.funnel.Y.back() + g_app.funnel.Y.front()), 0.0};
    double p = g_app.scene.pitch;
    double eye[3] = {center[0] + g_app.funnel_dist * std::cos(p) * std::cos(g_app.scene.yaw),
                     center[1] + g_app.funnel_dist * std::sin(p),
                     center[2] + g_app.funnel_dist * std::cos(p) * std::sin(g_app.scene.yaw)};
    double up[3] = {0.0, 1.0, 0.0};
    double V[16], P[16], M[16];
    look_at(eye, center, up, V);
    perspective(40.0 * M_PI / 180.0, aspect, 0.5, 400.0, P);
    mat_mul(P, V, M);
    FunnelUniforms U;
    for (int i = 0; i < 16; ++i) U.mvp[i] = float(M[i]);
    U.r_plus = float(g_app.funnel.r_plus);
    U.r_isco = float(g_app.disk.r_isco);
    U.r_ph = float(g_app.r_ph);
    U.r_view = float(g_app.funnel.r_view);
    U.y_top = float(g_app.funnel.Y.back());
    U.y_bottom = float(g_app.funnel.Y.front());
    U.pad0 = 0.0f;
    U.pad1 = 0.0f;
    return U;
}

// Source for the shader library: the shared headers are inlined (Metal has no local #include),
// and only the system include is kept.
NSString* read_text(NSString* path) {
    NSError* err = nil;
    NSString* s = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!s) {
        std::fprintf(stderr, "cannot read %s: %s\n", path.UTF8String, err.localizedDescription.UTF8String);
    }
    return s;
}

NSString* strip_local_includes(NSString* text) {
    NSMutableString* out = [NSMutableString string];
    for (NSString* line in [text componentsSeparatedByString:@"\n"]) {
        if ([line hasPrefix:@"#include \""]) continue;
        [out appendString:line];
        [out appendString:@"\n"];
    }
    return out;
}

NSString* build_shader_source() {
    NSString* shaders = [NSString stringWithUTF8String:BH_SHADER_DIR];
    NSString* src = [[shaders stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"src"];
    NSMutableString* all = [NSMutableString stringWithString:@"#include <metal_stdlib>\nusing namespace metal;\n"];
    for (NSString* name in @[ @"shared_types.h", @"kerr_core.h", @"shading.h" ]) {
        NSString* text = read_text([src stringByAppendingPathComponent:name]);
        if (!text) return nil;
        [all appendString:strip_local_includes(text)];
    }
    NSString* kernels = read_text([shaders stringByAppendingPathComponent:@"bh.metal"]);
    if (!kernels) return nil;
    [all appendString:strip_local_includes(kernels)];
    return all;
}

void apply_scene_geometry() {
    g_app.disk = build_disk(g_app.scene.a, 16.0, 4096);
    g_app.funnel = build_funnel_profile(g_app.scene.a, 14.0, 320);
    g_app.r_ph = photon_orbit_prograde(g_app.scene.a);
}

}  // namespace

// ---- Renderer ------------------------------------------------------------------------------------

@interface BHRenderer : NSObject <MTKViewDelegate>
- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)rebuildGeometry;
- (void)encodeInto:(id<MTLCommandBuffer>)cb
             color:(id<MTLTexture>)color
             depth:(id<MTLTexture>)depth
             width:(NSUInteger)w
            height:(NSUInteger)h
             scale:(double)scale;
@end

@implementation BHRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLComputePipelineState> _tracePSO;
    id<MTLRenderPipelineState> _blitPSO;
    id<MTLRenderPipelineState> _sheetPSO;
    id<MTLDepthStencilState> _depthState;
    id<MTLBuffer> _lutBuffer;
    id<MTLBuffer> _sheetBuffer;
    id<MTLTexture> _hdr;
    NSUInteger _hdrW, _hdrH;
    id<MTLTexture> _depth;
    NSUInteger _depthW, _depthH;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (!self) return nil;
    _device = device;
    _queue = [device newCommandQueue];

    NSString* source = build_shader_source();
    if (!source) return nil;
    MTLCompileOptions* options = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;   // no fast-math: keep float results close to the double reference
    }
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:source options:options error:&err];
    if (!lib) {
        std::fprintf(stderr, "shader compile failed:\n%s\n", err.localizedDescription.UTF8String);
        return nil;
    }
    _tracePSO = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"trace_kernel"] error:&err];
    if (!_tracePSO) {
        std::fprintf(stderr, "compute pipeline failed: %s\n", err.localizedDescription.UTF8String);
        return nil;
    }

    MTLRenderPipelineDescriptor* bd = [MTLRenderPipelineDescriptor new];
    bd.vertexFunction = [lib newFunctionWithName:@"blit_vertex"];
    bd.fragmentFunction = [lib newFunctionWithName:@"blit_fragment"];
    bd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    _blitPSO = [device newRenderPipelineStateWithDescriptor:bd error:&err];

    MTLRenderPipelineDescriptor* sd = [MTLRenderPipelineDescriptor new];
    sd.vertexFunction = [lib newFunctionWithName:@"funnel_vertex"];
    sd.fragmentFunction = [lib newFunctionWithName:@"funnel_fragment"];
    sd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    sd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    _sheetPSO = [device newRenderPipelineStateWithDescriptor:sd error:&err];
    if (!_blitPSO || !_sheetPSO) {
        std::fprintf(stderr, "render pipeline failed: %s\n", err.localizedDescription.UTF8String);
        return nil;
    }

    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    _depthState = [device newDepthStencilStateWithDescriptor:dd];

    [self rebuildGeometry];
    return self;
}

- (void)rebuildGeometry {
    apply_scene_geometry();
    const std::vector<float>& lut = g_app.disk.lut;
    _lutBuffer = [_device newBufferWithBytes:lut.data() length:lut.size() * sizeof(float) options:MTLResourceStorageModeShared];

    std::vector<FunnelVertex> mesh = build_funnel_mesh(g_app.funnel, 128);
    g_app.funnel_vertex_count = unsigned(mesh.size());
    _sheetBuffer = [_device newBufferWithBytes:mesh.data() length:mesh.size() * sizeof(FunnelVertex) options:MTLResourceStorageModeShared];
}

- (id<MTLTexture>)hdrWithWidth:(NSUInteger)w height:(NSUInteger)h {
    if (!_hdr || _hdrW != w || _hdrH != h) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        d.storageMode = MTLStorageModePrivate;
        _hdr = [_device newTextureWithDescriptor:d];
        _hdrW = w;
        _hdrH = h;
    }
    return _hdr;
}

- (id<MTLTexture>)depthWithWidth:(NSUInteger)w height:(NSUInteger)h {
    if (!_depth || _depthW != w || _depthH != h) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget;
        d.storageMode = MTLStorageModePrivate;
        _depth = [_device newTextureWithDescriptor:d];
        _depthW = w;
        _depthH = h;
    }
    return _depth;
}

- (void)encodeInto:(id<MTLCommandBuffer>)cb
             color:(id<MTLTexture>)color
             depth:(id<MTLTexture>)depth
             width:(NSUInteger)w
            height:(NSUInteger)h
             scale:(double)scale {
    if (g_app.view == 0) {
        NSUInteger tw = std::max<NSUInteger>(1, NSUInteger(double(w) * scale));
        NSUInteger th = std::max<NSUInteger>(1, NSUInteger(double(h) * scale));
        id<MTLTexture> hdr = [self hdrWithWidth:tw height:th];

        TraceUniforms U = build_uniforms(g_app.scene, g_app.disk, unsigned(tw), unsigned(th));
        id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
        [ce setComputePipelineState:_tracePSO];
        [ce setTexture:hdr atIndex:0];
        [ce setBytes:&U length:sizeof(U) atIndex:0];
        [ce setBuffer:_lutBuffer offset:0 atIndex:1];
        [ce dispatchThreads:MTLSizeMake(tw, th, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        [ce endEncoding];

        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = color;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
        [re setRenderPipelineState:_blitPSO];
        [re setFragmentTexture:hdr atIndex:0];
        float exposure = float(g_app.scene.exposure);
        [re setFragmentBytes:&exposure length:sizeof(exposure) atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    } else {
        id<MTLTexture> dtex = depth ? depth : [self depthWithWidth:w height:h];
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = color;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0.008, 0.008, 0.016, 1);
        rp.depthAttachment.texture = dtex;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;
        FunnelUniforms FU = make_funnel_uniforms(double(w) / double(h));
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
        [re setRenderPipelineState:_sheetPSO];
        [re setDepthStencilState:_depthState];
        [re setVertexBuffer:_sheetBuffer offset:0 atIndex:0];
        [re setVertexBytes:&FU length:sizeof(FU) atIndex:1];
        [re setFragmentBytes:&FU length:sizeof(FU) atIndex:1];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:g_app.funnel_vertex_count];
        [re endEncoding];
    }
}

- (void)drawInMTKView:(MTKView*)view {
    double now = CACurrentMediaTime();
    double dt = g_app.last_frame_time > 0.0 ? std::min(0.1, now - g_app.last_frame_time) : 0.0;
    g_app.last_frame_time = now;
    if (g_app.auto_orbit) {
        g_app.scene.yaw += 0.12 * dt;
    }

    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!drawable) return;
    id<MTLTexture> target = drawable.texture;
    NSUInteger w = target.width, h = target.height;

    id<MTLCommandBuffer> cb = [_queue commandBuffer];
    double scale = g_app.view == 0 ? g_app.render_scale : 1.0;
    [self encodeInto:cb color:target depth:nil width:w height:h scale:scale];
    [cb presentDrawable:drawable];
    [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        g_app.gpu_ms = (c.GPUEndTime - c.GPUStartTime) * 1000.0;
        if (g_app.view == 0 && g_app.adaptive_scale && g_app.gpu_ms > 0.0) {
            if (g_app.gpu_ms > 22.0) g_app.render_scale = std::max(0.35, g_app.render_scale * 0.96);
            if (g_app.gpu_ms < 12.0) g_app.render_scale = std::min(1.0, g_app.render_scale * 1.01);
        }
    }];
    [cb commit];

    if (now - g_app.last_title_update > 0.5) {
        g_app.last_title_update = now;
        NSString* title = [NSString stringWithFormat:@"Kerr a = %.3f  |  %s  |  %lux%lu @ %.0f%%  |  GPU %.1f ms  |  bg: %s%s  |  T_max %.2g K (10 M_sun, 1e-8 M_sun/yr)  |  eta %.3f",
                           g_app.scene.a,
                           g_app.view == 0 ? "lensing" : "spacetime sheet",
                           (unsigned long)w, (unsigned long)h,
                           g_app.view == 0 ? g_app.render_scale * 100.0 : 100.0,
                           g_app.gpu_ms,
                           kBackgroundNames[g_app.scene.bg_mode],
                           g_app.scene.disk_on ? "  |  disk on" : "  |  disk off",
                           g_app.disk.T_max_K,
                           g_app.scene.eta];
        view.window.title = title;
    }

    if (g_app.quit_after_frames) {
        if (++g_app.frames_done >= g_app.frames_to_run) {
            [NSApp terminate:nil];
        }
    }
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}

@end

// ---- Window and input ----------------------------------------------------------------------------

static BHRenderer* g_renderer = nil;

@interface BHView : MTKView
@end

@implementation BHView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)mouseDragged:(NSEvent*)event {
    g_app.scene.yaw -= event.deltaX * 0.006;
    g_app.scene.pitch = std::clamp(g_app.scene.pitch + event.deltaY * 0.006, -1.4, 1.4);
    g_app.auto_orbit = false;
}

- (void)scrollWheel:(NSEvent*)event {
    double k = std::exp(-event.scrollingDeltaY * 0.01);
    if (g_app.view == 0) {
        g_app.scene.dist = std::clamp(g_app.scene.dist * k, 8.0, 300.0);
    } else {
        g_app.funnel_dist = std::clamp(g_app.funnel_dist * k, 12.0, 200.0);
    }
}

- (void)magnifyWithEvent:(NSEvent*)event {
    double k = 1.0 / std::max(0.2, 1.0 + event.magnification);
    if (g_app.view == 0) {
        g_app.scene.dist = std::clamp(g_app.scene.dist * k, 8.0, 300.0);
    } else {
        g_app.funnel_dist = std::clamp(g_app.funnel_dist * k, 12.0, 200.0);
    }
}

- (void)keyDown:(NSEvent*)event {
    NSString* k = event.charactersIgnoringModifiers;
    if (k.length == 0) return;
    unichar c = [k characterAtIndex:0];
    Scene& s = g_app.scene;
    switch (c) {
        case '1': g_app.view = 0; break;
        case '2': g_app.view = 1; break;
        case '[': s.a = std::clamp(s.a - 0.02, 0.0, 0.998); [g_renderer rebuildGeometry]; break;
        case ']': s.a = std::clamp(s.a + 0.02, 0.0, 0.998); [g_renderer rebuildGeometry]; break;
        case 'b': s.bg_mode = (s.bg_mode + 1) % 3; break;
        case 'd': s.disk_on = !s.disk_on; break;
        case '+': case '=': s.exposure *= 1.15; break;
        case '-': case '_': s.exposure /= 1.15; break;
        case 'p': g_app.auto_orbit = !g_app.auto_orbit; break;
        case 'r': s.yaw = 0.0; s.pitch = 0.12; s.dist = 40.0; g_app.funnel_dist = 34.0; g_app.auto_orbit = true; break;
        case 'q': [NSApp terminate:nil]; break;
        default: break;
    }
}

@end

@interface BHAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BHAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}
@end

// ---- Snapshot ------------------------------------------------------------------------------------

static int run_snapshot(const char* path, NSUInteger w, NSUInteger h) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        std::fprintf(stderr, "no Metal device\n");
        return 1;
    }
    BHRenderer* renderer = [[BHRenderer alloc] initWithDevice:device];
    if (!renderer) return 1;

    MTLTextureDescriptor* cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModeShared;
    id<MTLTexture> color = [device newTextureWithDescriptor:cd];

    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    [renderer encodeInto:cb color:color depth:nil width:w height:h scale:1.0];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error) {
        std::fprintf(stderr, "GPU error: %s\n", cb.error.localizedDescription.UTF8String);
        return 1;
    }

    std::vector<uint8_t> bgra(size_t(w) * h * 4);
    [color getBytes:bgra.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        rgb[3 * i + 0] = bgra[4 * i + 2];
        rgb[3 * i + 1] = bgra[4 * i + 1];
        rgb[3 * i + 2] = bgra[4 * i + 0];
    }
    if (!write_png_rgb8(path, rgb.data(), int(w), int(h))) {
        std::fprintf(stderr, "could not write %s\n", path);
        return 1;
    }
    std::printf("wrote %s (%lux%lu, view %s, a = %.3f, GPU %.1f ms)\n", path, (unsigned long)w, (unsigned long)h,
                g_app.view == 0 ? "lensing" : "sheet", g_app.scene.a,
                (cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
    return 0;
}

// ---- Entry point ---------------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        std::string snapshot;
        unsigned width = 1200, height = 750;
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
            if (k == "--snapshot") snapshot = next();
            else if (k == "--width") width = unsigned(std::atoi(next()));
            else if (k == "--height") height = unsigned(std::atoi(next()));
            else if (k == "--view") g_app.view = std::string(next()) == "sheet" ? 1 : 0;
            else if (k == "--spin") g_app.scene.a = std::atof(next());
            else if (k == "--yaw") g_app.scene.yaw = std::atof(next());
            else if (k == "--pitch") g_app.scene.pitch = std::atof(next());
            else if (k == "--dist") g_app.scene.dist = std::atof(next());
            else if (k == "--exposure") g_app.scene.exposure = std::atof(next());
            else if (k == "--gain") g_app.scene.disk_gain = std::atof(next());
            else if (k == "--tpeak") g_app.scene.T_peak = std::atof(next());
            else if (k == "--eta") g_app.scene.eta = std::atof(next());
            else if (k == "--steps") g_app.scene.max_steps = std::atoi(next());
            else if (k == "--nodisk") g_app.scene.disk_on = false;
            else if (k == "--frames") { g_app.frames_to_run = std::atol(next()); g_app.quit_after_frames = true; }
            else if (k == "--bg") {
                std::string m = next();
                g_app.scene.bg_mode = m == "stars" ? 0 : (m == "grid" ? 1 : 2);
            } else {
                std::fprintf(stderr, "unknown option %s\n", k.c_str());
                return 2;
            }
        }
        g_app.scene.pitch = std::clamp(g_app.scene.pitch, -1.4, 1.4);
        g_app.render_scale = 1.0;

        if (!snapshot.empty()) {
            return run_snapshot(snapshot.c_str(), width, height);
        }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::fprintf(stderr, "no Metal device\n");
            return 1;
        }
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        BHAppDelegate* delegate = [BHAppDelegate new];
        [app setDelegate:delegate];

        g_renderer = [[BHRenderer alloc] initWithDevice:device];
        if (!g_renderer) return 1;

        NSRect frame = NSMakeRect(120, 120, width, height);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        BHView* view = [[BHView alloc] initWithFrame:frame device:device];
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.preferredFramesPerSecond = 60;
        view.enableSetNeedsDisplay = NO;
        view.paused = NO;
        view.delegate = g_renderer;
        window.contentView = view;
        window.title = @"Kerr black hole";
        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        [app activateIgnoringOtherApps:YES];

        std::printf("Controls: 1 lensing | 2 spacetime sheet | [ ] spin | b background | d disk | +/- exposure | "
                    "p pause orbit | r reset | drag orbit | scroll zoom | q quit\n");
        [app run];
    }
    return 0;
}
