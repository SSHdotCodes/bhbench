// Renderer.mm — Metal renderer implementation.
//
// Frame flow:
//   1. compute pass: raytraceKernel integrates one null geodesic per pixel
//      into an offscreen texture (size = drawable * renderScale).
//   2. render pass: fullscreen quad (the ray-traced image), then the 3D
//      spacetime grid — Flamm's paraboloid, the exact embedding diagram of
//      the Schwarzschild equatorial spatial slice:
//          z(r) = -2 sqrt(rs (r - rs))
//      Grid lines are colored by the Kretschmann curvature scalar
//      K = 12 rs²/r⁶ (sqrt(K) ∝ r⁻³). Highlight rings mark the photon
//      sphere (r = 1.5 rs) and the ISCO (r = 3 rs). Small "marbles" ride
//      circular geodesics with Omega = sqrt(M/r³) on the curved surface.

#import "Renderer.h"
#import "Params.h"
#import "MathUtil.h"
#import <Metal/Metal.h>
#import <AppKit/AppKit.h>
#include <vector>
#include <cmath>
#include <chrono>

struct MeshUniforms { simd_float4x4 mvp; simd_float4 color; };
struct Vtx { float x, y, z, r, g, b; };   // line vertex: position + color

static const float RS = 1.0f;             // Schwarzschild radius (geometric units)

// Flamm's paraboloid embedding height (dip downward)
static inline float flammZ(float r) { return -2.0f * sqrtf(RS * (r - RS)); }

@implementation Renderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    CAMetalLayer* _layer;

    id<MTLComputePipelineState> _computePipe;
    id<MTLRenderPipelineState> _quadPipe;
    id<MTLRenderPipelineState> _linePipe;
    id<MTLRenderPipelineState> _surfPipe;
    id<MTLRenderPipelineState> _spherePipe;
    id<MTLDepthStencilState> _depthOn;
    id<MTLDepthStencilState> _depthOff;
    id<MTLSamplerState> _sampler;

    id<MTLBuffer> _gridVtx;   NSUInteger _gridIdxCount;   id<MTLBuffer> _gridIdx;
    id<MTLBuffer> _ringPhoton; NSUInteger _ringPhotonCount; id<MTLBuffer> _ringPhotonIdx;
    id<MTLBuffer> _ringISCO;   NSUInteger _ringISCOCount;   id<MTLBuffer> _ringISCOIdx;
    id<MTLBuffer> _sphereVtx; NSUInteger _sphereIdxCount; id<MTLBuffer> _sphereIdx;

    id<MTLTexture> _offscreen;   NSUInteger _offW, _offH;
    id<MTLTexture> _depthTex;    NSUInteger _depW, _depH;
}

// ---------------------------------------------------------------------------

- (NSString*)findShaderSource {
    NSString* exe = [[NSBundle mainBundle] executablePath];
    NSString* dir = [exe stringByDeletingLastPathComponent];
    NSArray* candidates = @[
        [dir stringByAppendingPathComponent:@"Shaders.metal"],
        [dir stringByAppendingPathComponent:@"../src/Shaders.metal"],
        @"src/Shaders.metal",
        @"Shaders.metal",
    ];
    for (NSString* p in candidates)
        if ([[NSFileManager defaultManager] fileExistsAtPath:p])
            return [NSString stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:nil];
    return nil;
}

- (instancetype)initWithLayer:(CAMetalLayer*)layer error:(NSError**)err {
    if (!(self = [super init])) return nil;
    _layer = layer;
    _device = layer.device;
    if (!_device) _device = MTLCreateSystemDefaultDevice();
    if (!_device) { NSLog(@"Metal not available"); return nil; }
    layer.device = _device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;              // allow blit readback for screenshots
    _queue = [_device newCommandQueue];

    // --- shader library (compiled from source at startup) ---
    NSString* src = [self findShaderSource];
    if (!src) { NSLog(@"Shaders.metal not found"); return nil; }
    NSError* e = nil;
    id<MTLLibrary> lib = [_device newLibraryWithSource:src options:nil error:&e];
    if (!lib) { NSLog(@"Shader compile error: %@", e); return nil; }

    _computePipe = [_device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"raytraceKernel"] error:&e];
    if (!_computePipe) { NSLog(@"Compute pipeline error: %@", e); return nil; }

    // quad pipeline
    {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [lib newFunctionWithName:@"quadVert"];
        d.fragmentFunction = [lib newFunctionWithName:@"quadFrag"];
        d.colorAttachments[0].pixelFormat = layer.pixelFormat;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        _quadPipe = [_device newRenderPipelineStateWithDescriptor:d error:&e];
        if (!_quadPipe) { NSLog(@"Quad pipeline error: %@", e); return nil; }
    }
    // line pipeline (alpha blended, depth tested)
    {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [lib newFunctionWithName:@"lineVert"];
        d.fragmentFunction = [lib newFunctionWithName:@"lineFrag"];
        d.colorAttachments[0].pixelFormat = layer.pixelFormat;
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
        vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0;  vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat3; vd.attributes[1].offset = 12; vd.attributes[1].bufferIndex = 0;
        vd.layouts[0].stride = sizeof(Vtx);
        d.vertexDescriptor = vd;
        _linePipe = [_device newRenderPipelineStateWithDescriptor:d error:&e];
        if (!_linePipe) { NSLog(@"Line pipeline error: %@", e); return nil; }
    }
    // grid surface pipeline (translucent, depth tested + written)
    {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [lib newFunctionWithName:@"surfVert"];
        d.fragmentFunction = [lib newFunctionWithName:@"surfFrag"];
        d.colorAttachments[0].pixelFormat = layer.pixelFormat;
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
        vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0; vd.attributes[0].bufferIndex = 0;
        vd.layouts[0].stride = sizeof(simd_float3);
        d.vertexDescriptor = vd;
        _surfPipe = [_device newRenderPipelineStateWithDescriptor:d error:&e];
        if (!_surfPipe) { NSLog(@"Surface pipeline error: %@", e); return nil; }
    }
    // sphere pipeline
    {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [lib newFunctionWithName:@"sphereVert"];
        d.fragmentFunction = [lib newFunctionWithName:@"sphereFrag"];
        d.colorAttachments[0].pixelFormat = layer.pixelFormat;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
        vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0;  vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat3; vd.attributes[1].offset = 12; vd.attributes[1].bufferIndex = 0;
        vd.layouts[0].stride = sizeof(Vtx);
        d.vertexDescriptor = vd;
        _spherePipe = [_device newRenderPipelineStateWithDescriptor:d error:&e];
        if (!_spherePipe) { NSLog(@"Sphere pipeline error: %@", e); return nil; }
    }

    MTLDepthStencilDescriptor* dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = MTLCompareFunctionLess; dd.depthWriteEnabled = YES;
    _depthOn = [_device newDepthStencilStateWithDescriptor:dd];
    MTLDepthStencilDescriptor* dd2 = [[MTLDepthStencilDescriptor alloc] init];
    dd2.depthCompareFunction = MTLCompareFunctionAlways; dd2.depthWriteEnabled = NO;
    _depthOff = [_device newDepthStencilStateWithDescriptor:dd2];

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear; sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge; sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    _sampler = [_device newSamplerStateWithDescriptor:sd];

    [self buildGrid];
    [self buildSphere];
    return self;
}

// ---------------------------------------------------------------------------
// Spacetime grid: triangle mesh of Flamm's paraboloid (the exact embedding of
// the Schwarzschild equatorial spatial slice). Grid lines and curvature
// coloring are drawn procedurally in surfFrag.
// ---------------------------------------------------------------------------
- (void)buildGrid {
    const int NR = 90, NS = 128;               // rings, segments/ring
    const float rIn = RS * 1.01f, rOut = 18.0f * RS;
    std::vector<simd_float3> vtx;
    std::vector<unsigned int> idx;

    for (int i = 0; i <= NR; i++) {
        float r = rIn * powf(rOut / rIn, (float)i / NR);
        float z = flammZ(r);
        for (int j = 0; j <= NS; j++) {
            float th = 2.0f * M_PI * j / NS;
            vtx.push_back(simd_make_float3(r * cosf(th), z, r * sinf(th)));
        }
    }
    for (int i = 0; i < NR; i++)
        for (int j = 0; j < NS; j++) {
            unsigned a = i * (NS + 1) + j, b = a + NS + 1;
            idx.insert(idx.end(), { a, b, a + 1, b, b + 1, a + 1 });
        }
    _gridIdxCount = idx.size();
    _gridVtx = [_device newBufferWithBytes:vtx.data() length:vtx.size()*sizeof(simd_float3) options:MTLResourceStorageModeShared];
    _gridIdx = [_device newBufferWithBytes:idx.data() length:idx.size()*sizeof(unsigned int) options:MTLResourceStorageModeShared];

    // highlight rings: photon sphere (cyan) and ISCO (green)
    struct RingMesh { id<MTLBuffer> v; id<MTLBuffer> i; NSUInteger count; };
    auto makeRing = [&](float r, simd_float3 col) -> RingMesh {
        std::vector<Vtx> v; std::vector<unsigned int> ii;
        const int N = 160; float z = flammZ(r);
        for (int j = 0; j < N; j++) {
            float th = 2.0f * M_PI * j / N;
            v.push_back({ r * cosf(th), z + 0.02f, r * sinf(th), col.x, col.y, col.z });
        }
        for (int j = 0; j < N; j++) { ii.push_back(j); ii.push_back((j + 1) % N); }
        RingMesh rm;
        rm.v = [_device newBufferWithBytes:v.data() length:v.size()*sizeof(Vtx) options:MTLResourceStorageModeShared];
        rm.i = [_device newBufferWithBytes:ii.data() length:ii.size()*sizeof(unsigned int) options:MTLResourceStorageModeShared];
        rm.count = ii.size();
        return rm;
    };
    RingMesh rp = makeRing(1.5f * RS, simd_make_float3(0.2f, 0.9f, 1.0f));
    _ringPhoton = rp.v; _ringPhotonIdx = rp.i; _ringPhotonCount = rp.count;
    RingMesh ri = makeRing(3.0f * RS, simd_make_float3(0.4f, 1.0f, 0.4f));
    _ringISCO = ri.v; _ringISCOIdx = ri.i; _ringISCOCount = ri.count;
}

// ---------------------------------------------------------------------------
// Small UV sphere for the orbiting test-particle marbles.
// ---------------------------------------------------------------------------
- (void)buildSphere {
    const int stacks = 10, slices = 16;
    std::vector<Vtx> vtx; std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; i++) {
        float phi = M_PI * i / stacks;              // 0..pi
        for (int j = 0; j <= slices; j++) {
            float th = 2.0f * M_PI * j / slices;
            float x = sinf(phi) * cosf(th), y = cosf(phi), z = sinf(phi) * sinf(th);
            vtx.push_back({ x, y, z, x, y, z });    // normal = position
        }
    }
    for (int i = 0; i < stacks; i++)
        for (int j = 0; j < slices; j++) {
            unsigned a = i * (slices + 1) + j, b = a + slices + 1;
            idx.insert(idx.end(), { a, b, a + 1, b, b + 1, a + 1 });
        }
    _sphereIdxCount = idx.size();
    _sphereVtx = [_device newBufferWithBytes:vtx.data() length:vtx.size()*sizeof(Vtx) options:MTLResourceStorageModeShared];
    _sphereIdx = [_device newBufferWithBytes:idx.data() length:idx.size()*sizeof(unsigned int) options:MTLResourceStorageModeShared];
}

// ---------------------------------------------------------------------------

- (void)ensureOffscreen:(NSUInteger)w h:(NSUInteger)h {
    if (_offscreen && _offW == w && _offH == h) return;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    _offscreen = [_device newTextureWithDescriptor:td];
    _offW = w; _offH = h;
}

- (void)ensureDepth:(NSUInteger)w h:(NSUInteger)h {
    if (_depthTex && _depW == w && _depH == h) return;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                  width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    _depthTex = [_device newTextureWithDescriptor:td];
    _depW = w; _depH = h;
}

// ---------------------------------------------------------------------------

- (void)drawSnapshot:(const SimSnapshot&)snap time:(float)t drawableSize:(CGSize)size {
    NSUInteger dw = (NSUInteger)size.width, dh = (NSUInteger)size.height;
    if (dw < 8 || dh < 8) return;

    @autoreleasepool {
        id<CAMetalDrawable> drawable = [_layer nextDrawable];
        if (!drawable) return;

        // camera
        simd_float3 camPos = snap.dist * (simd_float3){
            cosf(snap.pitch) * cosf(snap.yaw), sinf(snap.pitch), cosf(snap.pitch) * sinf(snap.yaw) };
        simd_float3 fwd = simd_normalize(-camPos);
        simd_float3 right = simd_normalize(simd_cross(fwd, (simd_float3){0, 1, 0}));
        simd_float3 up = simd_cross(right, fwd);

        id<MTLCommandBuffer> cb = [_queue commandBuffer];

        // ---- 1. compute pass: geodesic ray tracing ----
        NSUInteger rw = 0, rh = 0;
        if (snap.bgRaytrace) {
            rw = (NSUInteger)(dw * snap.renderScale);
            rh = (NSUInteger)(dh * snap.renderScale);
            rw = MAX(rw, 8); rh = MAX(rh, 8);
            [self ensureOffscreen:rw h:rh];

            Params P;
            P.camPos = camPos;                 P.aspect = (float)rw / (float)rh;
            P.camRight = right;                P.tanHalfFov = tanf(38.0f * 0.5f * M_PI / 180.0f);
            P.camUp = up;                      P.time = t;
            P.camFwd = fwd;                    P.exposure = snap.exposure;
            P.diskNormal = simd_normalize((simd_float3){0, 1, 0});
            P.diskIn = 3.0f * RS;              // ISCO = 6M = 3 rs
            P.skyNormal = simd_normalize((simd_float3){0.5f, 0.3f, 0.8f});
            P.diskOut = 11.0f * RS;
            P.maxSteps = (unsigned)snap.maxSteps;
            P.flags = (snap.disk ? F_DISK : 0) | (snap.sky ? F_SKY : 0) |
                      (snap.beaming ? F_BEAMING : 0) | (snap.redshift ? F_REDSHIFT : 0);
            P.width = (unsigned)rw;            P.height = (unsigned)rh;
            P.rs = RS;                         P.diskBrightness = 0.8f;
            P.tempScale = 30000.0f;            // peak disk temperature (K)
            P.farR = 60.0f * RS;

            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:_computePipe];
            [ce setTexture:_offscreen atIndex:0];
            [ce setBytes:&P length:sizeof(P) atIndex:0];
            MTLSize tg = MTLSizeMake(8, 8, 1);
            MTLSize grid = MTLSizeMake((rw + 7) / 8, (rh + 7) / 8, 1);
            [ce dispatchThreadgroups:grid threadsPerThreadgroup:tg];
            [ce endEncoding];
        }

        // ---- 2. render pass: quad + grid overlay ----
        [self ensureDepth:dw h:dh];
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rp.depthAttachment.texture = _depthTex;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];

        if (snap.bgRaytrace) {
            [re setRenderPipelineState:_quadPipe];
            [re setDepthStencilState:_depthOff];
            [re setFragmentTexture:_offscreen atIndex:0];
            [re setFragmentSamplerState:_sampler atIndex:0];
            [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }

        if (snap.grid) {
            simd_float4x4 proj = matPerspective(38.0f * M_PI / 180.0f, (float)dw / (float)dh, 0.1f, 500.0f);
            simd_float4x4 view = matLookAt(camPos, simd_make_float3(0, 0, 0), simd_make_float3(0, 1, 0));
            simd_float4x4 vp = matMul(proj, view);

            MeshUniforms U; U.mvp = vp; U.color = {1, 1, 1, 1};
            // translucent paraboloid surface with procedural grid lines
            [re setRenderPipelineState:_surfPipe];
            [re setDepthStencilState:_depthOn];
            [re setVertexBuffer:_gridVtx offset:0 atIndex:0];
            [re setVertexBytes:&U length:sizeof(U) atIndex:1];
            [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_gridIdxCount
                            indexType:MTLIndexTypeUInt32 indexBuffer:_gridIdx indexBufferOffset:0];
            // photon sphere + ISCO rings
            [re setRenderPipelineState:_linePipe];
            [re setVertexBuffer:_ringPhoton offset:0 atIndex:0];
            [re drawIndexedPrimitives:MTLPrimitiveTypeLine indexCount:_ringPhotonCount
                            indexType:MTLIndexTypeUInt32 indexBuffer:_ringPhotonIdx indexBufferOffset:0];
            [re setVertexBuffer:_ringISCO offset:0 atIndex:0];
            [re drawIndexedPrimitives:MTLPrimitiveTypeLine indexCount:_ringISCOCount
                            indexType:MTLIndexTypeUInt32 indexBuffer:_ringISCOIdx indexBufferOffset:0];

            // marbles on circular geodesics: Omega = sqrt(M/r^3), M = 0.5
            static const float orbitR[3] = { 3.0f, 4.5f, 6.5f };
            static const simd_float3 marbleCol[3] = {
                {1.0f, 0.8f, 0.2f}, {0.3f, 0.9f, 1.0f}, {1.0f, 0.35f, 0.6f} };
            [re setRenderPipelineState:_spherePipe];
            for (int m = 0; m < 3; m++) {
                float r = orbitR[m];
                float omega = sqrtf(0.5f / (r * r * r));
                float th = omega * t * 2.5f + (float)m * 2.1f;
                simd_float3 pos = { r * cosf(th), flammZ(r), r * sinf(th) };
                MeshUniforms MU;
                MU.mvp = matMul(vp, matMul(matTranslate(pos), matScale(0.22f)));
                MU.color = { marbleCol[m].x, marbleCol[m].y, marbleCol[m].z, 1.0f };
                [re setVertexBuffer:_sphereVtx offset:0 atIndex:0];
                [re setVertexBytes:&MU length:sizeof(MU) atIndex:1];
                [re setFragmentBytes:&MU length:sizeof(MU) atIndex:1];
                [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_sphereIdxCount
                                indexType:MTLIndexTypeUInt32 indexBuffer:_sphereIdx indexBufferOffset:0];
            }
        }
        [re endEncoding];

        // ---- 3. optional screenshot (blit drawable -> shared buffer) ----
        id<MTLBuffer> shotBuf = nil;
        NSUInteger shotW = 0, shotH = 0, shotRow = 0;
        if (snap.shot) {
            shotW = dw; shotH = dh;
            shotRow = ((shotW * 4) + 255) & ~255ull;
            shotBuf = [_device newBufferWithLength:shotRow * shotH options:MTLResourceStorageModeShared];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:drawable.texture sourceSlice:0 sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(shotW, shotH, 1)
                     toBuffer:shotBuf destinationOffset:0 destinationBytesPerRow:shotRow
                     destinationBytesPerImage:shotRow * shotH];
            [blit endEncoding];
        }

        [cb presentDrawable:drawable];
        [cb commit];

        if (snap.shot && shotBuf) {
            [cb waitUntilCompleted];
            [self savePNG:shotBuf width:shotW height:shotH rowBytes:shotRow
                     path:[NSString stringWithUTF8String:snap.shotPath.c_str()]];
        }
    }
}

- (void)savePNG:(id<MTLBuffer>)buf width:(NSUInteger)w height:(NSUInteger)h
       rowBytes:(NSUInteger)rowBytes path:(NSString*)path {
    // BGRA -> RGBA swizzle
    std::vector<unsigned char> rgba(w * h * 4);
    const unsigned char* src = (const unsigned char*)buf.contents;
    for (NSUInteger y = 0; y < h; y++)
        for (NSUInteger x = 0; x < w; x++) {
            const unsigned char* p = src + y * rowBytes + x * 4;
            unsigned char* q = rgba.data() + (y * w + x) * 4;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = 255;
        }
    unsigned char* planes[1] = { rgba.data() };
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:planes pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [png writeToFile:path atomically:YES];
    NSLog(@"Screenshot saved to %@", path);
}

@end
