// renderer.mm — Metal implementation. Physics lives in kerr_geodesic.h (GPU+CPU) and physics.hpp (CPU).
#import "renderer.h"
#include "physics.hpp"
#include "shader_types.h"
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <cmath>

static double halton(int i, int b) { double f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }
static double deg2rad(double d) { return d * bh::PI / 180.0; }

struct SimState {
    double a = 0.9;
    double camR = 42.0, camTh = deg2rad(76.0), camPh = 0.0;
    double fovDeg = 48.0;
    double yaw = 0.0, pitch = 0.0;
    bool diskOn = true;
    int gridMode = 1;               // 0 off, 1 embedding funnel, 2 ray-traced equatorial grid, 3 both
    bool bloomOn = true, paused = false, autoOrbit = false, showHelp = false;
    double tScale = 40000.0, exposure = 1.0, turbulence = 0.35, timeScale = 12.0, simTime = 0.0;   // T₀ (see physics.hpp)
    double tPeak = 0.0;
    double renderScale = 0.5;
    double rDiskOut = 24.0;
    double eps = 0.05, epsAng = 0.05;
    int maxSteps = 6000;
    double sheetR = 55.0;
    double gridSpacing = 2.0;
    double sheetAlpha = 0.85;
    double sheetGhost = 0.0;       // alpha multiplier for the occluded (hidden-line) part of the sheet
    double limb = 1.0;
};

@implementation BHRenderer {
    MTKView *_view;
    NSTextField *_hud;
    id<MTLDevice> _dev;
    id<MTLCommandQueue> _queue;
    id<MTLLibrary> _lib;
    id<MTLComputePipelineState> _psoTrace, _psoShade, _psoBloomDown, _psoBloomBlur;
    id<MTLRenderPipelineState> _psoBlit, _psoGrid;
    id<MTLDepthStencilState> _dsWrite, _dsTest, _dsTestGE;
    id<MTLTexture> _gA, _gB, _hdr, _bloomA, _bloomB, _readback;
    id<MTLBuffer> _bbLut, _diskLut, _gridVB, _gridIB;
    NSUInteger _gridIndexCount;
    double _gridZMin;
    int _rw, _rh;                   // ray-traced resolution
    int _sampleIndex;
    bool _dirty, _meshDirty, _lutDirty;
    SimState _s;
    std::string _shaderDir;
    std::chrono::steady_clock::time_point _lastFrame, _lastHud;
    std::atomic<double> _gpuMs;
    double _fps;
    int _frameCount, _exitAfter;
    NSString *_screenshotPath;
    NSString *_dumpPath;
    bool _screenshotPending;
    bh::BlackbodyLUT _bb;
    std::string _lastError;
}

// ------------------------------------------------------------------ setup
- (instancetype)initWithView:(MTKView *)view hud:(NSTextField *)hud options:(const BHOptions &)opts {
    self = [super init];
    _view = view; _hud = hud;
    _dev = view.device;
    _queue = [_dev newCommandQueue];
    _s.a = opts.spin; _s.camTh = deg2rad(opts.inclinationDeg); _s.camR = opts.camR;
    _s.renderScale = opts.renderScale; _s.tScale = opts.tScale; _s.gridMode = opts.gridMode; _s.bloomOn = opts.bloom; _s.diskOn = opts.disk;
    _exitAfter = opts.frames; _screenshotPath = opts.screenshotPath; _screenshotPending = false; _dumpPath = opts.dumpPath;
    _shaderDir = opts.shaderDir ? std::string(opts.shaderDir.UTF8String) : "src/shaders";
    _dirty = _meshDirty = _lutDirty = true; _sampleIndex = 0; _rw = _rh = 0; _gpuMs = 0; _fps = 0; _frameCount = 0;
    _lastFrame = _lastHud = std::chrono::steady_clock::now();
    _bb = bh::blackbodyLUT(1024, 2.5, 7.0);
    _bbLut = [_dev newBufferWithBytes:_bb.rgb.data() length:_bb.rgb.size() * sizeof(float) options:MTLResourceStorageModeShared];
    [self buildPipelines];
    return self;
}

- (NSString *)readFile:(NSString *)name {
    NSString *path = [NSString stringWithFormat:@"%s/%@", _shaderDir.c_str(), name];
    NSError *err = nil;
    NSString *s = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!s) { NSLog(@"cannot read %@: %@", path, err); }
    return s;
}

- (BOOL)buildPipelines {
    // Inline the shared headers: runtime Metal compilation cannot resolve user #includes.
    NSString *src = [self readFile:@"kerr.metal"];
    NSString *types = [self readFile:@"../shader_types.h"];
    NSString *geo = [self readFile:@"../kerr_geodesic.h"];
    if (!src || !types || !geo) return NO;
    src = [src stringByReplacingOccurrencesOfString:@"#include \"shader_types.h\"" withString:types];
    src = [src stringByReplacingOccurrencesOfString:@"#include \"kerr_geodesic.h\"" withString:geo];
    MTLCompileOptions *opts = [MTLCompileOptions new];
    opts.mathMode = MTLMathModeRelaxed;                         // allow FMA contraction, but…
    opts.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;   // …keep sin/cos/sqrt precise
    NSError *err = nil;
    id<MTLLibrary> lib = [_dev newLibraryWithSource:src options:opts error:&err];
    if (!lib) { NSLog(@"Metal compile error:\n%@", err); _lastError = err.localizedDescription.UTF8String; return NO; }
    if (err) NSLog(@"Metal compile warnings:\n%@", err);
    _lib = lib;
    auto cps = [&](NSString *fn) -> id<MTLComputePipelineState> {
        NSError *e = nil;
        id<MTLComputePipelineState> p = [_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:fn] error:&e];
        if (!p) NSLog(@"pipeline %@ failed: %@", fn, e);
        return p;
    };
    _psoTrace = cps(@"traceKernel");
    _psoShade = cps(@"shadeKernel");
    _psoBloomDown = cps(@"bloomDownKernel");
    _psoBloomBlur = cps(@"bloomBlurKernel");

    MTLRenderPipelineDescriptor *rd = [MTLRenderPipelineDescriptor new];
    rd.vertexFunction = [lib newFunctionWithName:@"blitVertex"];
    rd.fragmentFunction = [lib newFunctionWithName:@"blitFragment"];
    rd.colorAttachments[0].pixelFormat = _view.colorPixelFormat;
    rd.depthAttachmentPixelFormat = _view.depthStencilPixelFormat;
    _psoBlit = [_dev newRenderPipelineStateWithDescriptor:rd error:&err];
    if (!_psoBlit) NSLog(@"blit pipeline failed: %@", err);

    MTLRenderPipelineDescriptor *gd = [MTLRenderPipelineDescriptor new];
    gd.vertexFunction = [lib newFunctionWithName:@"gridVertex"];
    gd.fragmentFunction = [lib newFunctionWithName:@"gridFragment"];
    gd.colorAttachments[0].pixelFormat = _view.colorPixelFormat;
    gd.colorAttachments[0].blendingEnabled = YES;
    gd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    gd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    gd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;              // premultiplied
    gd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    gd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    gd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    gd.depthAttachmentPixelFormat = _view.depthStencilPixelFormat;
    _psoGrid = [_dev newRenderPipelineStateWithDescriptor:gd error:&err];
    if (!_psoGrid) NSLog(@"grid pipeline failed: %@", err);

    MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
    dsd.depthCompareFunction = MTLCompareFunctionAlways; dsd.depthWriteEnabled = YES;
    _dsWrite = [_dev newDepthStencilStateWithDescriptor:dsd];
    dsd.depthCompareFunction = MTLCompareFunctionLess; dsd.depthWriteEnabled = NO;
    _dsTest = [_dev newDepthStencilStateWithDescriptor:dsd];
    dsd.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    _dsTestGE = [_dev newDepthStencilStateWithDescriptor:dsd];
    _lastError.clear();
    return YES;
}

- (void)ensureTextures {
    CGSize ds = _view.drawableSize;
    int w = std::max(16, (int)std::lround(ds.width * _s.renderScale));
    int h = std::max(16, (int)std::lround(ds.height * _s.renderScale));
    if (w == _rw && h == _rh && _gA) return;
    _rw = w; _rh = h; _dirty = true;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:w height:h mipmapped:NO];
    td.textureType = MTLTextureType2DArray; td.arrayLength = BH_MAX_LAYERS;
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; td.storageMode = MTLStorageModePrivate;
    _gA = [_dev newTextureWithDescriptor:td];
    _gB = [_dev newTextureWithDescriptor:td];
    MTLTextureDescriptor *hd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
    hd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; hd.storageMode = MTLStorageModePrivate;
    _hdr = [_dev newTextureWithDescriptor:hd];
    MTLTextureDescriptor *bd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:std::max(1, w / 4) height:std::max(1, h / 4) mipmapped:NO];
    bd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; bd.storageMode = MTLStorageModePrivate;
    _bloomA = [_dev newTextureWithDescriptor:bd];
    _bloomB = [_dev newTextureWithDescriptor:bd];
}

- (void)rebuildDiskLUT {
    const double rIsco = bh::isco(_s.a);
    double peak = 0.0;
    std::vector<float> lut = bh::diskTemperatureProfile(_s.a, rIsco, _s.rDiskOut, 1024, &peak);
    _s.tPeak = peak;
    _diskLut = [_dev newBufferWithBytes:lut.data() length:lut.size() * sizeof(float) options:MTLResourceStorageModeShared];
    _lutDirty = false;
}

- (void)rebuildGridMesh {
    auto emb = bh::embedding(_s.a, _s.sheetR, 180);
    const int nR = (int)emb.size(), nP = 256;
    std::vector<float> vb; vb.reserve(nR * nP * 3);
    for (int i = 0; i < nR; ++i)
        for (int j = 0; j < nP; ++j) {
            const double ph = 2 * bh::PI * j / nP;
            vb.push_back((float)(emb[i].R * std::cos(ph)));
            vb.push_back((float)(emb[i].R * std::sin(ph)));
            vb.push_back((float)emb[i].Z);
        }
    std::vector<uint32_t> ib; ib.reserve((nR - 1) * nP * 6);
    for (int i = 0; i < nR - 1; ++i)
        for (int j = 0; j < nP; ++j) {
            const uint32_t a0 = i * nP + j, a1 = i * nP + (j + 1) % nP, b0 = (i + 1) * nP + j, b1 = (i + 1) * nP + (j + 1) % nP;
            ib.insert(ib.end(), {a0, b0, a1, a1, b0, b1});
        }
    _gridVB = [_dev newBufferWithBytes:vb.data() length:vb.size() * sizeof(float) options:MTLResourceStorageModeShared];
    _gridIB = [_dev newBufferWithBytes:ib.data() length:ib.size() * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    _gridIndexCount = ib.size();
    _gridZMin = emb.front().Z;
    _meshDirty = false;
}

// ------------------------------------------------------------------ camera helpers
- (void)cameraBasisRight:(simd_double3 *)right up:(simd_double3 *)up fwd:(simd_double3 *)fwd pos:(simd_double3 *)pos {
    const double st = std::sin(_s.camTh), ct = std::cos(_s.camTh), sp = std::sin(_s.camPh), cp = std::cos(_s.camPh);
    const simd_double3 rhat = {st * cp, st * sp, ct};
    const simd_double3 that = {ct * cp, ct * sp, -st};
    const simd_double3 phat = {-sp, cp, 0.0};
    // local-frame basis (components along r̂, θ̂, φ̂) after yaw/pitch, matching traceKernel
    auto rot = [&](simd_double3 v) {
        const double cy = std::cos(_s.yaw), sy = std::sin(_s.yaw);
        simd_double3 r1 = {v.x * cy - v.z * sy, v.y, v.x * sy + v.z * cy};
        const double cpi = std::cos(_s.pitch), spi = std::sin(_s.pitch);
        return simd_double3{r1.x * cpi - r1.y * spi, r1.x * spi + r1.y * cpi, r1.z};
    };
    auto toWorld = [&](simd_double3 v) { return v.x * rhat + v.y * that + v.z * phat; };
    *fwd = toWorld(rot((simd_double3){-1, 0, 0}));
    *up = toWorld(rot((simd_double3){0, -1, 0}));
    *right = toWorld(rot((simd_double3){0, 0, 1}));
    *pos = _s.camR * rhat;
}

// ------------------------------------------------------------------ frame
- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size { _dirty = true; if (_exitAfter > 0) std::fprintf(stderr, "drawableSizeWillChange %gx%g\n", size.width, size.height); }

- (void)drawInMTKView:(MTKView *)view {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - _lastFrame).count();
    _lastFrame = now;
    dt = std::min(dt, 0.1);
    _fps = _fps == 0 ? 1.0 / std::max(dt, 1e-4) : 0.95 * _fps + 0.05 / std::max(dt, 1e-4);
    if (!_s.paused) _s.simTime += dt * _s.timeScale;
    if (_s.autoOrbit) { _s.camPh += dt * 0.12; _dirty = true; }

    [self ensureTextures];
    if (_lutDirty) [self rebuildDiskLUT];
    if (_meshDirty) [self rebuildGridMesh];
    if (_dirty) { _sampleIndex = 0; _dirty = false; if (_exitAfter > 0) std::fprintf(stderr, "reset samples (frame %d)\n", _frameCount); }
    if (!_psoTrace || !_psoShade || !_psoBlit) return;

    id<CAMetalDrawable> drawable = view.currentDrawable;
    MTLRenderPassDescriptor *rpd = view.currentRenderPassDescriptor;
    if (!drawable || !rpd) return;

    const double rh = bh::horizon(_s.a), rIsco = bh::isco(_s.a);
    const double tanHalf = std::tan(deg2rad(_s.fovDeg) * 0.5);
    const double aspect = (double)_rw / _rh;
    const bool gridPlane = (_s.gridMode & 2) != 0;
    const bool funnel = (_s.gridMode & 1) != 0;

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];

    // ---- 1. geodesic trace into the next sample layer (only while the image is still converging)
    if (_sampleIndex < BH_MAX_LAYERS) {
        TraceUniforms U = {};
        U.a = (float)_s.a; U.rh = (float)rh; U.rIsco = (float)rIsco; U.rDiskOut = (float)_s.rDiskOut;
        U.rGridOut = (float)_s.sheetR; U.rEsc = (float)std::max(120.0, 2.5 * _s.camR);
        U.rStop = (float)(rh * 1.005); U.rDoom = (float)(bh::photonOrbit(std::fabs(_s.a)) * 0.999);
        U.camR = (float)_s.camR; U.camTh = (float)_s.camTh; U.camPh = (float)_s.camPh; U.tanHalfFov = (float)tanHalf;
        U.aspect = (float)aspect;
        U.jitterX = (float)(_sampleIndex == 0 ? 0.5 : halton(_sampleIndex, 2));
        U.jitterY = (float)(_sampleIndex == 0 ? 0.5 : halton(_sampleIndex, 3));
        U.eps = (float)_s.eps; U.epsAng = (float)_s.epsAng;
        U.camX = (float)(_s.camR * std::sin(_s.camTh) * std::cos(_s.camPh));
        U.camY = (float)(_s.camR * std::sin(_s.camTh) * std::sin(_s.camPh));
        U.camZ = (float)(_s.camR * std::cos(_s.camTh));
        U.yaw = (float)_s.yaw; U.pitch = (float)_s.pitch;
        U.width = _rw; U.height = _rh; U.layer = _sampleIndex; U.maxSteps = _s.maxSteps;
        U.diskOn = _s.diskOn ? 1 : 0; U.gridOn = gridPlane ? 1 : 0;
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:_psoTrace];
        [ce setTexture:_gA atIndex:0]; [ce setTexture:_gB atIndex:1];
        [ce setBytes:&U length:sizeof U atIndex:0];
        [ce dispatchThreads:MTLSizeMake(_rw, _rh, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [ce endEncoding];
        ++_sampleIndex;
    }
    const int layers = std::max(1, std::min(_sampleIndex, BH_MAX_LAYERS));

    // ---- 2. shade
    {
        ShadeUniforms U = {};
        U.a = (float)_s.a; U.rIsco = (float)rIsco; U.rDiskOut = (float)_s.rDiskOut; U.rGridOut = (float)_s.sheetR;
        U.tMax = (float)_s.tScale; U.time = (float)_s.simTime; U.pixelAngle = (float)(2.0 * tanHalf / _rh);
        U.turbulence = (float)_s.turbulence; U.logTmin = (float)_bb.logTmin; U.logTmax = (float)_bb.logTmax;
        U.gridSpacing = (float)_s.gridSpacing; U.skyBrightness = 1.0f; U.rh = (float)rh; U.limbDarkening = (float)_s.limb;
        U.width = _rw; U.height = _rh; U.layers = layers; U.lutN = _bb.n; U.diskLutN = 1024; U.flags = 0;
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:_psoShade];
        [ce setTexture:_gA atIndex:0]; [ce setTexture:_gB atIndex:1]; [ce setTexture:_hdr atIndex:2];
        [ce setBytes:&U length:sizeof U atIndex:0];
        [ce setBuffer:_bbLut offset:0 atIndex:1]; [ce setBuffer:_diskLut offset:0 atIndex:2];
        [ce dispatchThreads:MTLSizeMake(_rw, _rh, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [ce endEncoding];
    }

    // ---- 3. bloom (cosmetic)
    if (_s.bloomOn && _psoBloomDown && _psoBloomBlur) {
        BloomUniforms U = {};
        U.threshold = 1.2f; U.knee = 0.6f;
        U.srcW = _rw; U.srcH = _rh; U.dstW = (unsigned)_bloomA.width; U.dstH = (unsigned)_bloomA.height;
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:_psoBloomDown];
        [ce setTexture:_hdr atIndex:0]; [ce setTexture:_bloomA atIndex:1];
        [ce setBytes:&U length:sizeof U atIndex:0];
        [ce dispatchThreads:MTLSizeMake(U.dstW, U.dstH, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        U.srcW = U.dstW; U.srcH = U.dstH;
        [ce setComputePipelineState:_psoBloomBlur];
        U.dirX = 1; U.dirY = 0;
        [ce setTexture:_bloomA atIndex:0]; [ce setTexture:_bloomB atIndex:1];
        [ce setBytes:&U length:sizeof U atIndex:0];
        [ce dispatchThreads:MTLSizeMake(U.dstW, U.dstH, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        U.dirX = 0; U.dirY = 1;
        [ce setTexture:_bloomB atIndex:0]; [ce setTexture:_bloomA atIndex:1];
        [ce setBytes:&U length:sizeof U atIndex:0];
        [ce dispatchThreads:MTLSizeMake(U.dstW, U.dstH, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [ce endEncoding];
    }

    // ---- 4. tone-mapped blit (writes ray-traced depth) + embedding-diagram mesh
    const double depthFar = 400.0;
    {
        rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.clearDepth = 1.0;
        id<MTLRenderCommandEncoder> re = [cmd renderCommandEncoderWithDescriptor:rpd];
        BlitUniforms BU = {};
        BU.exposure = (float)_s.exposure; BU.bloomStrength = 0.35f; BU.depthFar = (float)depthFar;
        BU.width = (unsigned)drawable.texture.width; BU.height = (unsigned)drawable.texture.height;
        BU.flags = _s.bloomOn ? 1u : 0u;
        [re setRenderPipelineState:_psoBlit];
        [re setDepthStencilState:_dsWrite];
        [re setFragmentTexture:_hdr atIndex:0]; [re setFragmentTexture:_bloomA atIndex:1];
        [re setFragmentBytes:&BU length:sizeof BU atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

        if (funnel && _psoGrid && _gridIB) {
            simd_double3 right, up, fwd, pos;
            [self cameraBasisRight:&right up:&up fwd:&fwd pos:&pos];
            const double nearZ = 0.05, farZ = 2000.0, A = farZ / (farZ - nearZ), B = -farZ * nearZ / (farZ - nearZ);
            const double ta = tanHalf * ((double)drawable.texture.width / drawable.texture.height);
            simd_double4 r0 = {right.x / ta, right.y / ta, right.z / ta, -simd_dot(pos, right) / ta};
            simd_double4 r1 = {up.x / tanHalf, up.y / tanHalf, up.z / tanHalf, -simd_dot(pos, up) / tanHalf};
            simd_double4 r2 = {A * fwd.x, A * fwd.y, A * fwd.z, -A * simd_dot(pos, fwd) + B};
            simd_double4 r3 = {fwd.x, fwd.y, fwd.z, -simd_dot(pos, fwd)};
            GridUniforms GU = {};
            simd_float4x4 M;
            M.columns[0] = (simd_float4){(float)r0.x, (float)r1.x, (float)r2.x, (float)r3.x};
            M.columns[1] = (simd_float4){(float)r0.y, (float)r1.y, (float)r2.y, (float)r3.y};
            M.columns[2] = (simd_float4){(float)r0.z, (float)r1.z, (float)r2.z, (float)r3.z};
            M.columns[3] = (simd_float4){(float)r0.w, (float)r1.w, (float)r2.w, (float)r3.w};
            GU.viewProj = M;
            GU.camPos = (simd_float3){(float)pos.x, (float)pos.y, (float)pos.z};
            GU.depthFar = (float)depthFar; GU.rh = (float)rh; GU.gridSpacing = (float)_s.gridSpacing;
            GU.alpha = (float)_s.sheetAlpha; GU.zMin = (float)_gridZMin; GU.rOut = (float)_s.sheetR; GU.time = (float)_s.simTime;
            [re setRenderPipelineState:_psoGrid];
            [re setCullMode:MTLCullModeNone];
            [re setVertexBuffer:_gridVB offset:0 atIndex:0];
            // pass 1: hidden part of the sheet (behind the disk / shadow) as a faint ghost
            if (_s.sheetGhost > 0.0) {
                GridUniforms G2 = GU; G2.alpha = (float)(_s.sheetAlpha * _s.sheetGhost);
                [re setDepthStencilState:_dsTestGE];
                [re setVertexBytes:&G2 length:sizeof G2 atIndex:1];
                [re setFragmentBytes:&G2 length:sizeof G2 atIndex:1];
                [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_gridIndexCount indexType:MTLIndexTypeUInt32 indexBuffer:_gridIB indexBufferOffset:0];
            }
            // pass 2: visible part
            [re setDepthStencilState:_dsTest];
            [re setVertexBytes:&GU length:sizeof GU atIndex:1];
            [re setFragmentBytes:&GU length:sizeof GU atIndex:1];
            [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_gridIndexCount indexType:MTLIndexTypeUInt32 indexBuffer:_gridIB indexBufferOffset:0];
        }
        [re endEncoding];
    }

    // ---- 5. optional readback for screenshots
    const bool wantShot = _screenshotPending || (_exitAfter > 0 && _frameCount + 1 >= _exitAfter && _screenshotPath);
    if (wantShot) {
        id<MTLTexture> src = drawable.texture;
        if (!_readback || _readback.width != src.width || _readback.height != src.height) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:src.width height:src.height mipmapped:NO];
            td.storageMode = MTLStorageModeShared; td.usage = MTLTextureUsageShaderRead;
            _readback = [_dev newTextureWithDescriptor:td];
        }
        id<MTLBlitCommandEncoder> be = [cmd blitCommandEncoder];
        [be copyFromTexture:src toTexture:_readback];
        [be endEncoding];
    }

    __weak BHRenderer *weakSelf = self;
    const int frameNo = _frameCount, samplesNow = _sampleIndex;
    const bool traced = samplesNow > 0 && layers == samplesNow && samplesNow <= BH_MAX_LAYERS && (_exitAfter > 0);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        BHRenderer *s = weakSelf; if (!s) return;
        const double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
        s->_gpuMs = ms;
        if (s->_exitAfter > 0) std::fprintf(stderr, "frame %d: GPU %.2f ms (%s, %d AA layers shaded)%s\n", frameNo, ms, traced ? "trace+shade" : "shade only", layers, cb.error ? "  ERROR" : "");
    }];
    [cmd presentDrawable:drawable];
    [cmd commit];
    ++_frameCount;

    if (wantShot) {
        [cmd waitUntilCompleted];
        NSString *path = _screenshotPending ? _screenshotPath : _screenshotPath;
        if (!path) path = [NSString stringWithFormat:@"blackhole_%d.png", _frameCount];
        [self saveTexture:_readback toPNG:path];
        _screenshotPending = false;
        if (_dumpPath) [self dumpGBuffer:_dumpPath];
        if (_exitAfter > 0 && _frameCount >= _exitAfter) { [NSApp terminate:nil]; return; }
    } else if (_exitAfter > 0 && _frameCount >= _exitAfter) {
        [NSApp terminate:nil]; return;
    }

    if (std::chrono::duration<double>(now - _lastHud).count() > 0.25) { _lastHud = now; [self updateHud]; }
}

- (void)saveTexture:(id<MTLTexture>)tex toPNG:(NSString *)path {
    const NSUInteger w = tex.width, h = tex.height;
    std::vector<uint8_t> buf(w * h * 4);
    [tex getBytes:buf.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    for (size_t i = 0; i < w * h; ++i) { std::swap(buf[4 * i], buf[4 * i + 2]); buf[4 * i + 3] = 255; }   // BGRA → RGBA
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
    memcpy(rep.bitmapData, buf.data(), buf.size());
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if ([png writeToFile:path atomically:YES]) NSLog(@"saved %@ (%lux%lu)", path, (unsigned long)w, (unsigned long)h);
    else NSLog(@"failed to write %@", path);
}

// Debug: read back G-buffer layer 0 and print statistics (hit types, step counts, NaNs).
- (void)dumpGBuffer:(NSString *)path {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:_rw height:_rh mipmapped:NO];
    td.storageMode = MTLStorageModeShared; td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> ta = [_dev newTextureWithDescriptor:td], tb = [_dev newTextureWithDescriptor:td];
    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    id<MTLBlitCommandEncoder> be = [cmd blitCommandEncoder];
    [be copyFromTexture:_gA sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(_rw, _rh, 1) toTexture:ta destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [be copyFromTexture:_gB sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(_rw, _rh, 1) toTexture:tb destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [be endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
    std::vector<float> A(_rw * _rh * 4), B(_rw * _rh * 4);
    [ta getBytes:A.data() bytesPerRow:_rw * 16 fromRegion:MTLRegionMake2D(0, 0, _rw, _rh) mipmapLevel:0];
    [tb getBytes:B.data() bytesPerRow:_rw * 16 fromRegion:MTLRegionMake2D(0, 0, _rw, _rh) mipmapLevel:0];
    long cnt[4] = {0, 0, 0, 0}, nan = 0, maxed = 0; double stepsSum = 0; float stepsMax = 0;
    std::vector<unsigned char> img(_rw * _rh);
    for (int i = 0; i < _rw * _rh; ++i) {
        int t = (int)(A[4 * i] + 0.5f); if (t < 0 || t > 3) t = 2;
        ++cnt[t]; stepsSum += B[4 * i + 3]; stepsMax = std::max(stepsMax, B[4 * i + 3]);
        if (B[4 * i + 3] >= _s.maxSteps) ++maxed;
        if (std::isnan(A[4 * i + 1]) || std::isnan(A[4 * i + 2]) || std::isnan(A[4 * i + 3]) || std::isnan(B[4 * i])) ++nan;
        img[i] = t == 0 ? 60 : t == 1 ? 200 : t == 3 ? 130 : 0;
        if (B[4 * i + 3] >= _s.maxSteps) img[i] = 255;
    }
    std::printf("G-buffer %dx%d: sky %ld  disk %ld  horizon %ld  grid %ld | NaN payloads %ld | steps mean %.1f max %.0f, maxSteps hit %ld\n",
                _rw, _rh, cnt[0], cnt[1], cnt[2], cnt[3], nan, stepsSum / (_rw * _rh), stepsMax, maxed);
    // a few sample pixels down the centre column
    for (int y = 0; y < _rh; y += _rh / 12) {
        const int i = y * _rw + _rw / 2 + 20;
        std::printf("  px(%d,%d): type %.0f  a=(%.4f %.4f %.4f) depth %.2f dt %.2f w %.4f steps %.0f\n", _rw / 2 + 20, y, A[4 * i], A[4 * i + 1], A[4 * i + 2], A[4 * i + 3], B[4 * i], B[4 * i + 1], B[4 * i + 2], B[4 * i + 3]);
    }
    FILE *f = fopen(path.UTF8String, "wb");
    if (f) { std::fprintf(f, "P5\n%d %d\n255\n", _rw, _rh); fwrite(img.data(), 1, img.size(), f); fclose(f); std::printf("wrote type map %s\n", path.UTF8String); }
    // HDR readback (RGBA16F) along a column and a row
    MTLTextureDescriptor *hd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:_rw height:_rh mipmapped:NO];
    hd.storageMode = MTLStorageModeShared; hd.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> th = [_dev newTextureWithDescriptor:hd];
    id<MTLCommandBuffer> cmd2 = [_queue commandBuffer];
    id<MTLBlitCommandEncoder> be2 = [cmd2 blitCommandEncoder];
    [be2 copyFromTexture:_hdr toTexture:th]; [be2 endEncoding]; [cmd2 commit]; [cmd2 waitUntilCompleted];
    std::vector<uint16_t> Hh(_rw * _rh * 4);
    [th getBytes:Hh.data() bytesPerRow:_rw * 8 fromRegion:MTLRegionMake2D(0, 0, _rw, _rh) mipmapLevel:0];
    auto h2f = [](uint16_t h) { uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff; float v; if (e == 0) v = ldexpf((float)m, -24); else if (e == 31) v = m ? NAN : INFINITY; else v = ldexpf((float)(m | 0x400), (int)e - 25); return s ? -v : v; };
    long nanH = 0; for (size_t i = 0; i < Hh.size(); ++i) if (std::isnan(h2f(Hh[i]))) ++nanH;
    std::printf("HDR NaN count: %ld\n", nanH);
    // per-layer statistics in the columns around the centre
    for (int layer = 0; layer < std::min(_sampleIndex, BH_MAX_LAYERS); ++layer) {
        id<MTLCommandBuffer> c3 = [_queue commandBuffer];
        id<MTLBlitCommandEncoder> b3 = [c3 blitCommandEncoder];
        [b3 copyFromTexture:_gA sourceSlice:layer sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(_rw, _rh, 1) toTexture:ta destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b3 copyFromTexture:_gB sourceSlice:layer sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(_rw, _rh, 1) toTexture:tb destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b3 endEncoding]; [c3 commit]; [c3 waitUntilCompleted];
        [ta getBytes:A.data() bytesPerRow:_rw * 16 fromRegion:MTLRegionMake2D(0, 0, _rw, _rh) mipmapLevel:0];
        [tb getBytes:B.data() bytesPerRow:_rw * 16 fromRegion:MTLRegionMake2D(0, 0, _rw, _rh) mipmapLevel:0];
        long nanL = 0, ct[4] = {0, 0, 0, 0}, mx = 0; int minx = 1 << 30, maxx = -1, miny = 1 << 30, maxy = -1;
        for (int y = 0; y < _rh; ++y) for (int x = 0; x < _rw; ++x) {
            const int i = y * _rw + x;
            bool bad = std::isnan(A[4 * i]) || std::isnan(A[4 * i + 1]) || std::isnan(A[4 * i + 2]) || std::isnan(A[4 * i + 3]) || std::isnan(B[4 * i]) || std::isnan(B[4 * i + 1]) || std::isnan(B[4 * i + 2]);
            int t = (int)(A[4 * i] + 0.5f); if (t < 0 || t > 3) { t = 2; bad = true; }
            if (B[4 * i + 3] >= _s.maxSteps) ++mx;
            ++ct[t];
            if (bad) { ++nanL; minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        }
        std::printf("  layer %d: sky %ld disk %ld horizon %ld grid %ld | bad %ld (x %d..%d, y %d..%d) | maxSteps %ld\n", layer, ct[0], ct[1], ct[2], ct[3], nanL, minx, maxx, miny, maxy, mx);
        if (nanL) { const int i = miny * _rw + (minx + maxx) / 2; std::printf("     sample bad px (%d,%d): A=(%g %g %g %g) B=(%g %g %g %g)\n", (minx + maxx) / 2, miny, A[4*i], A[4*i+1], A[4*i+2], A[4*i+3], B[4*i], B[4*i+1], B[4*i+2], B[4*i+3]); }
    }
    const int y0 = 40;
    for (int x = _rw / 2 - 60; x <= _rw / 2 + 60; x += 10) {
        const int i = y0 * _rw + x;
        std::printf("  hdr(%d,%d) = (%.4f %.4f %.4f | depth %.1f)  type %.0f dir (%.4f %.4f %.4f) gsky %.4f\n", x, y0, h2f(Hh[4 * i]), h2f(Hh[4 * i + 1]), h2f(Hh[4 * i + 2]), h2f(Hh[4 * i + 3]), A[4 * i], A[4 * i + 1], A[4 * i + 2], A[4 * i + 3], B[4 * i + 2]);
    }
}

- (void)updateHud {
    if (!_hud) return;
    static const char *gridNames[] = {"off", "embedding funnel", "lensed equatorial grid", "funnel + lensed grid"};
    NSString *line1 = [NSString stringWithFormat:@"Kerr black hole   a = %.3f   r+ = %.3f M   r_isco = %.3f M   r_ph(pro/retro) = %.2f/%.2f M",
                       _s.a, bh::horizon(_s.a), bh::isco(_s.a), bh::photonOrbit(_s.a), bh::photonOrbit(-_s.a)];
    NSString *line2 = [NSString stringWithFormat:@"camera r = %.1f M   inclination = %.1f°   fov = %.0f°   exposure %.2f   t = %.0f M%s   |   disk T0 = %.0f K, T_peak = %.0f K, η = %.1f%%  (≈ 10⁸ M☉ at Ṁ = %.1e Ṁ_Edd)",
                       _s.camR, _s.camTh * 180.0 / bh::PI, _s.fovDeg, _s.exposure, _s.simTime, _s.paused ? " (paused)" : "",
                       _s.tScale, _s.tScale * _s.tPeak, 100.0 * bh::efficiency(_s.a), bh::eddingtonRatio(_s.tScale, 1e8, _s.a)];
    NSString *line3 = [NSString stringWithFormat:@"disk %s   grid: %s   bloom %s   %dx%d ray-traced, %d/%d AA samples   GPU %.1f ms   %.0f fps%s",
                       _s.diskOn ? "on" : "off", gridNames[_s.gridMode & 3], _s.bloomOn ? "on" : "off", _rw, _rh,
                       std::min(_sampleIndex, BH_MAX_LAYERS), BH_MAX_LAYERS, (double)_gpuMs, _fps, _s.autoOrbit ? "   auto-orbit" : ""];
    NSString *help = @"drag: orbit   right-drag: look   scroll: zoom   [ ]: spin   , .: disk T   - =: exposure   D: disk   G: grid mode   B: bloom   P: pause   Space: auto-orbit   1/2/3: resolution   T: turbulence   L: limb darkening   X: ghost sheet   F/V: fov   I/K/J/O: orbit   Z: reset look   S: screenshot   R: reload shaders   Q: quit";
    NSString *text = _s.showHelp ? [NSString stringWithFormat:@"%@\n%@\n%@\n%@", line1, line2, line3, help]
                                 : [NSString stringWithFormat:@"%@\n%@\n%@\nH: help", line1, line2, line3];
    if (!_lastError.empty()) text = [text stringByAppendingFormat:@"\nSHADER ERROR: %s", _lastError.c_str()];
    _hud.stringValue = text;
}

// ------------------------------------------------------------------ input
- (void)keyDown:(NSEvent *)event {
    NSString *ch = event.charactersIgnoringModifiers;
    if (ch.length == 0) return;
    const unichar c = [ch characterAtIndex:0];
    switch (c) {
        case '[': _s.a = std::max(-0.998, _s.a - 0.05); _dirty = _meshDirty = _lutDirty = true; break;
        case ']': _s.a = std::min(0.998, _s.a + 0.05); _dirty = _meshDirty = _lutDirty = true; break;
        case ',': _s.tScale = std::max(4000.0, _s.tScale / 1.15); break;
        case '.': _s.tScale = std::min(1.0e7, _s.tScale * 1.15); break;
        case '-': _s.exposure = std::max(0.02, _s.exposure / 1.25); break;
        case '=': case '+': _s.exposure = std::min(50.0, _s.exposure * 1.25); break;
        case 'd': case 'D': _s.diskOn = !_s.diskOn; _dirty = true; break;
        case 'g': case 'G': _s.gridMode = (_s.gridMode + 1) & 3; _dirty = true; break;
        case 'b': case 'B': _s.bloomOn = !_s.bloomOn; break;
        case 'p': case 'P': _s.paused = !_s.paused; break;
        case ' ': _s.autoOrbit = !_s.autoOrbit; break;
        case '1': _s.renderScale = 0.35; break;
        case '2': _s.renderScale = 0.5; break;
        case '3': _s.renderScale = 1.0; break;
        case 't': case 'T': _s.turbulence = _s.turbulence > 0.01 ? 0.0 : 0.35; break;
        case 'l': case 'L': _s.limb = _s.limb > 0.5 ? 0.0 : 1.0; break;
        case 'f': case 'F': _s.fovDeg = std::max(20.0, _s.fovDeg - 5.0); _dirty = true; break;
        case 'v': case 'V': _s.fovDeg = std::min(110.0, _s.fovDeg + 5.0); _dirty = true; break;
        case 'i': case 'I': _s.camTh = std::clamp(_s.camTh + deg2rad(2.0), deg2rad(1.0), deg2rad(179.0)); _dirty = true; break;
        case 'k': case 'K': _s.camTh = std::clamp(_s.camTh - deg2rad(2.0), deg2rad(1.0), deg2rad(179.0)); _dirty = true; break;
        case 'j': case 'J': _s.camPh -= deg2rad(3.0); _dirty = true; break;
        case 'o': case 'O': _s.camPh += deg2rad(3.0); _dirty = true; break;
        case 'z': case 'Z': _s.yaw = _s.pitch = 0.0; _dirty = true; break;
        case 'x': case 'X': _s.sheetGhost = _s.sheetGhost > 0.0 ? 0.0 : 0.2; break;
        case 's': case 'S': _screenshotPending = true; if (!_screenshotPath) _screenshotPath = [NSString stringWithFormat:@"blackhole_%ld.png", (long)time(nullptr)]; break;
        case 'r': case 'R': [self buildPipelines]; _dirty = true; break;
        case 'h': case 'H': _s.showHelp = !_s.showHelp; break;
        case 'q': case 'Q': case 27: [NSApp terminate:nil]; break;
        case NSUpArrowFunctionKey: _s.camTh = std::clamp(_s.camTh - deg2rad(2.0), deg2rad(1.0), deg2rad(179.0)); _dirty = true; break;
        case NSDownArrowFunctionKey: _s.camTh = std::clamp(_s.camTh + deg2rad(2.0), deg2rad(1.0), deg2rad(179.0)); _dirty = true; break;
        case NSLeftArrowFunctionKey: _s.camPh -= deg2rad(3.0); _dirty = true; break;
        case NSRightArrowFunctionKey: _s.camPh += deg2rad(3.0); _dirty = true; break;
        default: break;
    }
    [self updateHud];
}

- (void)mouseDrag:(CGFloat)dx dy:(CGFloat)dy secondary:(BOOL)secondary {
    if (dx == 0 && dy == 0) return;
    if (secondary) {
        _s.yaw = std::clamp(_s.yaw - dx * 0.003, -1.2, 1.2);
        _s.pitch = std::clamp(_s.pitch + dy * 0.003, -1.2, 1.2);
    } else {
        _s.camPh -= dx * 0.005;
        _s.camTh = std::clamp(_s.camTh - dy * 0.005, deg2rad(1.0), deg2rad(179.0));
    }
    _dirty = true;
}

- (void)scroll:(CGFloat)dy {
    if (dy == 0) return;
    _s.camR = std::clamp(_s.camR * std::exp(-dy * 0.02), 4.0, 200.0);
    _dirty = true;
}

- (void)requestScreenshot:(NSString *)path { _screenshotPath = path; _screenshotPending = true; }
@end
