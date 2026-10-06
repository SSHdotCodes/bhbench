// Black Hole: real-time geodesic ray tracer (Metal + MPS)
// Obj-C++ shell: window, MTKView, pipeline states, MPSImage bloom, input.

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MTKView.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include "shaders.hpp"
#include "sim.hpp"

using namespace bh;

// layouts must match shaders.hpp
struct URT {
    float cam_x, cam_y, cam_z, cam_r;
    float fwd_x, fwd_y, fwd_z, tan_fov;
    float rgt_x, rgt_y, rgt_z, aspect;
    float up_x, up_y, up_z, time;
};

struct UMesh {
    Mat4 proj;
    Mat4 view;
    float time, pad0, pad1, pad2;
};

static const float FOV = 0.9f;
static const float CAM_NEAR = 0.5f;
static const float CAM_FAR = 600.0f;

// ---------------------------------------------------------------------------
@interface App : NSObject <MTKViewDelegate, NSApplicationDelegate>
- (void)start;
- (void)render;
- (void)mouseDown:(NSEvent *)e;
- (void)mouseDragged:(NSEvent *)e;
- (void)scrollWheel:(NSEvent *)e;
- (void)keyDown:(NSEvent *)e;
@end

@interface BHView : MTKView
@property (weak) App *app;
@end

@implementation BHView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDown:(NSEvent *)e { [self.app mouseDown:e]; }
- (void)mouseDragged:(NSEvent *)e { [self.app mouseDragged:e]; }
- (void)scrollWheel:(NSEvent *)e { [self.app scrollWheel:e]; }
- (void)keyDown:(NSEvent *)e { [self.app keyDown:e]; }
@end

// ---------------------------------------------------------------------------
@implementation App {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLLibrary> _lib;
    BHView *_view;
    NSWindow *_win;

    id<MTLComputePipelineState> _rayPS, _downPS, _blurHPS, _blurVPS;
    id<MTLRenderPipelineState> _compPS, _funnelPS, _capPS, _linePS, _hudPS;

    id<MTLTexture> _rtTex, _bloomA, _bloomB, _depthTex, _hudTex;
    id<MTLBuffer> _lineBuf, _lineIdxBuf, _funnelVtxBuf, _funnelIdxBuf, _capVtxBuf, _capIdxBuf;
    id<MTLSamplerState> _smp;
    id<MTLDepthStencilState> _dsWrite, _dsRead;

    MPSImageGaussianBlur *_blur;
    BOOL _mpsOk;
    BOOL _mpsTested;
    BOOL _bloomInB;

    Sim _sim;
    NSPoint _lastMouse;
    float _fps;
    double _fpsT0;
    int _fpsN;
}

- (void)start {
    _device = MTLCreateSystemDefaultDevice();
    if (!_device) { NSLog(@"no Metal device"); exit(1); }
    _queue = [_device newCommandQueue];

    NSError *err = nil;
    _lib = [_device newLibraryWithSource:[NSString stringWithUTF8String:shaders::MSL]
                                 options:nil
                                   error:&err];
    if (err) { NSLog(@"MSL compile failed: %@", err); exit(1); }

    [self makePSOs];

    _sim.init();
    _sim.setMode(1);

    // static geometry buffers
    _funnelVtxBuf = [_device newBufferWithBytes:_sim.funnelV.data()
                                         length:_sim.funnelV.size() * 4
                                        options:MTLResourceStorageModeShared];
    _funnelIdxBuf = [_device newBufferWithBytes:_sim.funnelI.data()
                                         length:_sim.funnelI.size() * 4
                                        options:MTLResourceStorageModeShared];
    _capVtxBuf = [_device newBufferWithBytes:_sim.capV.data()
                                      length:_sim.capV.size() * 4
                                     options:MTLResourceStorageModeShared];
    _capIdxBuf = [_device newBufferWithBytes:_sim.capI.data()
                                      length:_sim.capI.size() * 4
                                     options:MTLResourceStorageModeShared];
    _lineBuf = [_device newBufferWithLength:Sim::NP * Sim::TRAIL * 7 * 4
                                    options:MTLResourceStorageModeShared];
    _lineIdxBuf = [_device newBufferWithBytes:_sim.lineI.data()
                                        length:_sim.lineI.size() * 4
                                       options:MTLResourceStorageModeShared];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    _smp = [_device newSamplerStateWithDescriptor:sd];

    MTLTextureDescriptor *hd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                    width:2048
                                   height:160
                                mipmapped:NO];
    hd.storageMode = MTLStorageModeShared;
    hd.usage = MTLTextureUsageShaderRead;
    _hudTex = [_device newTextureWithDescriptor:hd];

    MTLDepthStencilDescriptor *dw = [MTLDepthStencilDescriptor new];
    dw.depthCompareFunction = MTLCompareFunctionLess;
    dw.depthWriteEnabled = YES;
    _dsWrite = [_device newDepthStencilStateWithDescriptor:dw];
    MTLDepthStencilDescriptor *dr = [MTLDepthStencilDescriptor new];
    dr.depthCompareFunction = MTLCompareFunctionLess;
    dr.depthWriteEnabled = NO;
    _dsRead = [_device newDepthStencilStateWithDescriptor:dr];

    // window
    NSRect frame = NSMakeRect(0, 0, 1280, 800);
    _win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_win setTitle:@"Black Hole — Schwarzschild Geodesic Ray Tracer"];
    _view = [[BHView alloc] initWithFrame:frame];
    _view.app = self;
    _view.device = _device;
    _view.delegate = self;
    _view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    _view.depthStencilPixelFormat = MTLPixelFormatInvalid;
    _view.clearColor = MTLClearColorMake(0, 0, 0, 1);
    _view.preferredFramesPerSecond = 60;
    _view.framebufferOnly = YES;
    [_win setContentView:_view];
    [_win center];
    [_win makeFirstResponder:_view];
    [_win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)makePSOs {
    id<MTLFunction> fRay = [_lib newFunctionWithName:@"raytrace"];
    id<MTLFunction> fDown = [_lib newFunctionWithName:@"downsample"];
    id<MTLFunction> fBH = [_lib newFunctionWithName:@"blurH"];
    id<MTLFunction> fBV = [_lib newFunctionWithName:@"blurV"];
    _rayPS = [_device newComputePipelineStateWithFunction:fRay error:NULL];
    _downPS = [_device newComputePipelineStateWithFunction:fDown error:NULL];
    _blurHPS = [_device newComputePipelineStateWithFunction:fBH error:NULL];
    _blurVPS = [_device newComputePipelineStateWithFunction:fBV error:NULL];
    if (!_rayPS || !_downPS || !_blurHPS || !_blurVPS) {
        NSLog(@"compute pipeline creation failed");
        exit(1);
    }

    auto make = [self](NSString *vs, NSString *fs, BOOL depth, BOOL blend, BOOL additive) {
        MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [_lib newFunctionWithName:vs];
        d.fragmentFunction = [_lib newFunctionWithName:fs];
        MTLRenderPipelineColorAttachmentDescriptor *ca = d.colorAttachments[0];
        ca.pixelFormat = MTLPixelFormatBGRA8Unorm;
        if (depth) d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        if (blend) {
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = MTLBlendOperationAdd;
            ca.alphaBlendOperation = MTLBlendOperationAdd;
            if (additive) {
                ca.sourceRGBBlendFactor = MTLBlendFactorOne;
                ca.destinationRGBBlendFactor = MTLBlendFactorOne;
                ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
                ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
            } else {
                ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                ca.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
                ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            }
        }
        id<MTLRenderPipelineState> pso = [_device newRenderPipelineStateWithDescriptor:d error:NULL];
        if (!pso) { NSLog(@"PSO failed for %@", vs); exit(1); }
        return pso;
    };

    _compPS = make(@"fsq", @"composite", NO, NO, NO);
    _capPS = make(@"funnelVS", @"capFS", YES, NO, NO);
    _funnelPS = make(@"funnelVS", @"funnelFS", YES, YES, NO);
    _linePS = make(@"lineVS", @"lineFS", YES, YES, YES);
    _hudPS = make(@"hudVS", @"hudFS", NO, YES, NO);
}

- (void)makeRTTextures:(int)iW h:(int)iH {
    if (_rtTex && (int)_rtTex.width == iW && (int)_rtTex.height == iH) return;
    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                    width:iW height:iH mipmapped:NO];
    d.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    _rtTex = [_device newTextureWithDescriptor:d];

    int bw = (iW / 4) / 2 * 2, bh_ = (iH / 4) / 2 * 2;
    if (bw < 2) bw = 2;
    if (bh_ < 2) bh_ = 2;
    MTLTextureDescriptor *db = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                    width:bw height:bh_ mipmapped:NO];
    db.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    _bloomA = [_device newTextureWithDescriptor:db];
    _bloomB = [_device newTextureWithDescriptor:db];

    MTLTextureDescriptor *dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                    width:iW height:iH mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    _depthTex = [_device newTextureWithDescriptor:dd];
}

// one-time MPS self-test (falls back to compute blur on failure)
- (void)testMPS {
    if (_mpsTested) return;
    _mpsTested = YES;
    _blur = [[MPSImageGaussianBlur alloc] initWithDevice:_device sigma:5.0f];
    if (!_blur) { NSLog(@"MPS unavailable, using compute blur"); return; }

    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                    width:32 height:32 mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> a = [_device newTextureWithDescriptor:d];
    id<MTLTexture> b = [_device newTextureWithDescriptor:d];

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    [_blur encodeToCommandBuffer:cmd sourceTexture:a destinationTexture:b];
    [cmd commit];
    [cmd waitUntilCompleted];
    _mpsOk = (cmd.error == nil);
    if (_mpsOk) NSLog(@"MPS Image GaussianBlur self-test passed");
    if (!_mpsOk) {
        NSLog(@"MPS self-test failed ( %@ ), using compute blur", cmd.error);
        _blur = nil;
    }
}

// ---------------------------------------------------------------------------
- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size { (void)view; (void)size; }

- (void)drawInMTKView:(MTKView *)view { (void)view; [self render]; }

- (void)render {
    Sim *s = &_sim;
    int w = (int)_view.drawableSize.width, h = (int)_view.drawableSize.height;
    if (w < 4 || h < 4) return;

    double now = CACurrentMediaTime();
    _fpsN++;
    if (now - _fpsT0 >= 0.5) {
        _fps = _fpsN / (float)(now - _fpsT0);
        _fpsN = 0;
        _fpsT0 = now;
    }

    float dt = 1.0f / 60.0f;
    if (!s->paused) s->update(dt);

    // camera basis
    float eye[3];
    s->cam.position(eye);
    float fwd[3] = {-eye[0], -eye[1], -eye[2]};
    float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;
    float rgt[3] = {fwd[1], -fwd[0], 0.0f};
    float rl = sqrtf(rgt[0] * rgt[0] + rgt[1] * rgt[1]);
    if (rl < 1e-5f) { rgt[0] = 1; rgt[1] = 0; } else { rgt[0] /= rl; rgt[1] /= rl; }
    float upv[3] = {rgt[1] * fwd[2] - rgt[2] * fwd[1],
                    rgt[2] * fwd[0] - rgt[0] * fwd[2],
                    rgt[0] * fwd[1] - rgt[1] * fwd[0]};
    float rCam = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);

    int iw = (int)llroundf(w * s->resScale), ih = (int)llroundf(h * s->resScale);
    iw = MAX(64, (iw / 2) * 2);
    ih = MAX(36, (ih / 2) * 2);
    [self makeRTTextures:iw h:ih];
    [self testMPS];

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    if (!cmd) return;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> c) {
        if (c.error) NSLog(@"frame error: %@", c.error);
    }];

    // ---- 1: ray trace
    id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
    URT urt;
    urt.cam_x = eye[0]; urt.cam_y = eye[1]; urt.cam_z = eye[2]; urt.cam_r = rCam;
    urt.fwd_x = fwd[0]; urt.fwd_y = fwd[1]; urt.fwd_z = fwd[2];
    urt.tan_fov = tanf(FOV * 0.5f);
    urt.rgt_x = rgt[0]; urt.rgt_y = rgt[1]; urt.rgt_z = rgt[2];
    urt.aspect = (float)iw / (float)ih;
    urt.up_x = upv[0]; urt.up_y = upv[1]; urt.up_z = upv[2];
    urt.time = s->time;
    [ce setComputePipelineState:_rayPS];
    [ce setTexture:_rtTex atIndex:0];
    [ce setBytes:&urt length:sizeof(urt) atIndex:0];
    [ce dispatchThreadgroups:MTLSizeMake((iw + 7) / 8, (ih + 7) / 8, 1)
        threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [ce endEncoding];

    // ---- 2: downsample
    id<MTLComputeCommandEncoder> cd = [cmd computeCommandEncoder];
    [cd setComputePipelineState:_downPS];
    [cd setTexture:_rtTex atIndex:0];
    [cd setTexture:_bloomA atIndex:1];
    [cd dispatchThreadgroups:MTLSizeMake((int)((_bloomA.width + 15) / 16),
                                         (int)((_bloomA.height + 15) / 16), 1)
        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [cd endEncoding];

    // ---- 3: bloom blur
    _bloomInB = NO;
    if (_mpsOk) {
        [_blur encodeToCommandBuffer:cmd sourceTexture:_bloomA destinationTexture:_bloomB];
        _bloomInB = YES;
    } else {
        id<MTLComputeCommandEncoder> c1 = [cmd computeCommandEncoder];
        [c1 setComputePipelineState:_blurHPS];
        [c1 setTexture:_bloomA atIndex:0];
        [c1 setTexture:_bloomB atIndex:1];
        [c1 dispatchThreadgroups:MTLSizeMake((int)((_bloomB.width + 15) / 16),
                                             (int)((_bloomB.height + 15) / 16), 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [c1 endEncoding];
        id<MTLComputeCommandEncoder> c2 = [cmd computeCommandEncoder];
        [c2 setComputePipelineState:_blurVPS];
        [c2 setTexture:_bloomB atIndex:0];
        [c2 setTexture:_bloomA atIndex:1];
        [c2 dispatchThreadgroups:MTLSizeMake((int)((_bloomA.width + 15) / 16),
                                             (int)((_bloomA.height + 15) / 16), 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [c2 endEncoding];
    }

    // ---- 4: composite to drawable
    id<CAMetalDrawable> dr = [_view currentDrawable];
    if (!dr) return;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new];
    rp.colorAttachments[0].texture = dr.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> re = [cmd renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:_compPS];
    [re setFragmentSamplerState:_smp atIndex:0];
    [re setFragmentTexture:_rtTex atIndex:0];
    [re setFragmentTexture:_bloomInB ? _bloomB : _bloomA atIndex:1];
    [re drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [re endEncoding];

    // ---- 5: spacetime grid + photon geodesics
    if (s->gridOn) {
        MTLRenderPassDescriptor *rp2 = [MTLRenderPassDescriptor new];
        rp2.colorAttachments[0].texture = dr.texture;
        rp2.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp2.colorAttachments[0].storeAction = MTLStoreActionStore;
        MTLRenderPassDepthAttachmentDescriptor *dd2 = [MTLRenderPassDepthAttachmentDescriptor new];
        dd2.texture = _depthTex;
        dd2.loadAction = MTLLoadActionClear;
        dd2.storeAction = MTLStoreActionDontCare;
        rp2.depthAttachment = dd2;

        float center[3] = {0, 0, 0};
        float upw[3] = {0, 0, 1};
        UMesh um;
        um.view = matLookAt(eye, center, upw);
        um.proj = matPerspective(FOV, (float)w / (float)h, CAM_NEAR, CAM_FAR);
        um.time = s->time;
        um.pad0 = um.pad1 = um.pad2 = 0.0f;

        std::vector<float> lines;
        s->buildLines(lines);
        memcpy([_lineBuf contents], lines.data(), lines.size() * 4);

        id<MTLRenderCommandEncoder> re2 = [cmd renderCommandEncoderWithDescriptor:rp2];

        // cap (opaque, depth write)
        [re2 setRenderPipelineState:_capPS];
        [re2 setVertexBuffer:_capVtxBuf offset:0 atIndex:0];
        [re2 setVertexBytes:&um length:sizeof(um) atIndex:1];
        [re2 setDepthStencilState:_dsWrite];
        [re2 drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                         indexCount:s->capNi
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:_capIdxBuf
                indexBufferOffset:0];
        // funnel (translucent, depth read)
        [re2 setRenderPipelineState:_funnelPS];
        [re2 setVertexBuffer:_funnelVtxBuf offset:0 atIndex:0];
        [re2 setVertexBytes:&um length:sizeof(um) atIndex:1];
        [re2 setFragmentBytes:&um length:sizeof(um) atIndex:0];
        [re2 setDepthStencilState:_dsRead];
        [re2 drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                         indexCount:s->funnelNi
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:_funnelIdxBuf
                indexBufferOffset:0];
        // photon trails (additive line segments)
        [re2 setRenderPipelineState:_linePS];
        [re2 setVertexBuffer:_lineBuf offset:0 atIndex:0];
        [re2 setVertexBytes:&um length:sizeof(um) atIndex:1];
        [re2 setDepthStencilState:_dsRead];
        [re2 drawIndexedPrimitives:MTLPrimitiveTypeLine
                         indexCount:s->lineNi
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:_lineIdxBuf
                indexBufferOffset:0];
        [re2 endEncoding];
    }

    // ---- 6: HUD
    if (s->hudOn) {
        std::string txt = s->hudText(_fps, iw, ih);
        std::vector<uint8_t> px;
        rasterHud(txt, 2048, 160, px);
        [_hudTex replaceRegion:MTLRegionMake2D(0, 0, 2048, 160)
                   mipmapLevel:0
                   withBytes:px.data()
                   bytesPerRow:2048 * 4];
        MTLRenderPassDescriptor *rp3 = [MTLRenderPassDescriptor new];
        rp3.colorAttachments[0].texture = dr.texture;
        rp3.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp3.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> re3 = [cmd renderCommandEncoderWithDescriptor:rp3];
        [re3 setRenderPipelineState:_hudPS];
        float q[4] = {-0.985f, 0.97f, 0.20f * (float)w / (float)h, 0.15f};
        [re3 setVertexBytes:q length:sizeof(q) atIndex:0];
        [re3 setFragmentSamplerState:_smp atIndex:0];
        [re3 setFragmentTexture:_hudTex atIndex:0];
        [re3 drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re3 drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:1 vertexCount:3];
        [re3 endEncoding];
    }

    [cmd presentDrawable:dr];
    [cmd commit];

    if (s->autoRes) {
        if (_fps > 0 && _fps < 47 && s->resScale > 0.25f)
            s->resScale = MAX(0.25f, s->resScale - 0.1f);
        else if (_fps > 56 && s->resScale < 1.0f)
            s->resScale = MIN(1.0f, s->resScale + 0.1f);
    }
}

// ---------------------------------------------------------------------------
- (void)mouseDown:(NSEvent *)e { _lastMouse = [e locationInWindow]; }

- (void)mouseDragged:(NSEvent *)e {
    NSPoint p = [e locationInWindow];
    float dx = (float)(p.x - _lastMouse.x);
    float dy = (float)(p.y - _lastMouse.y);
    _lastMouse = p;
    _sim.cam.yaw += dx * 0.005f;
    _sim.cam.pitch = MIN(1.52f, MAX(0.03f, _sim.cam.pitch + dy * 0.005f));
}

- (void)scrollWheel:(NSEvent *)e {
    _sim.cam.dist = MIN(220.0f, MAX(8.0f, _sim.cam.dist * (float)exp(-e.scrollingDeltaY * 0.002)));
}

- (void)keyDown:(NSEvent *)e {
    NSString *c = [e charactersIgnoringModifiers];
    if (c.length == 0) return;
    unichar k = [c characterAtIndex:0];
    switch (k) {
        case '1': _sim.setMode(1); break;
        case '2': _sim.setMode(2); break;
        case '3': _sim.setMode(3); break;
        case 'g': case 'G': _sim.gridOn = !_sim.gridOn; break;
        case ' ': _sim.paused = !_sim.paused; break;
        case 'r': case 'R': _sim.setMode(1); _sim.paused = false; break;
        case 'a': case 'A': _sim.autoRes = !_sim.autoRes; break;
        case 'h': case 'H': _sim.hudOn = !_sim.hudOn; break;
        case '+': case '=': _sim.resScale = MIN(1.0f, _sim.resScale + 0.1f); break;
        case '-': case '_': _sim.resScale = MAX(0.25f, _sim.resScale - 0.1f); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    (void)app;
    return YES;
}

@end

// ---------------------------------------------------------------------------
int main(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        App *app = [App new];
        [app start];
        [NSApp run];
    }
    return 0;
}
