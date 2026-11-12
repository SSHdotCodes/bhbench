#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <mach-o/dyld.h>

#include "MetalApp.h"
#include "../core/Math.hpp"
#include "../core/BlackHolePhysics.hpp"
#include "../core/SimulationState.hpp"
#include "../core/Camera.hpp"
#include "../shaders/ShaderTypes.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace bh;

namespace {

std::string executableDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        return std::filesystem::path(buf.c_str()).parent_path().string();
    }
    return ".";
}

std::vector<GridVertex> buildSpacetimeGridMesh(double spinA, float rOuter, int numRings, int numSegs, std::vector<uint32_t>& outIndices) {
    double rp = physics::eventHorizonRadius(spinA);
    constexpr int kThroatRings = 12;
    int totalRings = numRings + kThroatRings;

    std::vector<double> ringR(totalRings);
    std::vector<double> ringZ(totalRings);

    double zHorizon = physics::kerrEquatorialEmbeddingZ(rp, spinA, rOuter);
    double throatDepth = 4.2;

    // Causal trapdoor throat interior extension below r_+
    for (int i = 0; i < kThroatRings; ++i) {
        double frac = static_cast<double>(i) / static_cast<double>(kThroatRings); // 0 .. <1
        double t = 1.0 - frac; // 1 at bottom, -> 0 at horizon
        double r = rp * (0.72 + 0.28 * frac);
        ringR[i] = r;
        ringZ[i] = zHorizon - throatDepth * std::pow(t, 1.35);
    }

    // Exact Kerr equatorial spatial embedding (reduces to Flamm's paraboloid at a=0)
    // Scaled gently (0.55x) so the full trapdoor throat stays framed within camera FOV
    for (int i = 0; i < numRings; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(numRings - 1);
        double r = rp + (static_cast<double>(rOuter) - rp) * (t * t);
        ringR[kThroatRings + i] = r;
        ringZ[kThroatRings + i] = 0.52 * physics::kerrEquatorialEmbeddingZ(r, spinA, rOuter);
    }
    zHorizon = ringZ[kThroatRings];
    throatDepth = 2.8;
    for (int i = 0; i < kThroatRings; ++i) {
        double frac = static_cast<double>(i) / static_cast<double>(kThroatRings);
        double t = 1.0 - frac;
        double r = rp * (0.72 + 0.28 * frac);
        ringR[i] = r;
        ringZ[i] = zHorizon - throatDepth * std::pow(t, 1.35);
    }

    std::vector<GridVertex> vertices;
    vertices.reserve(static_cast<size_t>(totalRings) * static_cast<size_t>(numSegs + 1));

    for (int i = 0; i < totalRings; ++i) {
        double r = ringR[i];
        double z = ringZ[i];

        // Finite-difference slope dz/dr for smooth surface normal
        int iPrev = std::max(0, i - 1);
        int iNext = std::min(totalRings - 1, i + 1);
        double dr = std::max(1e-4, ringR[iNext] - ringR[iPrev]);
        double dz = ringZ[iNext] - ringZ[iPrev];
        double slope = dz / dr;

        // Kerr frame-dragging twist angle
        float twist = static_cast<float>(2.4 * spinA * std::pow(rp / std::max(r, rp * 0.72), 2.2));

        for (int j = 0; j <= numSegs; ++j) {
            double phi = (2.0 * kPi * static_cast<double>(j)) / static_cast<double>(numSegs);
            float cosP = static_cast<float>(std::cos(phi));
            float sinP = static_cast<float>(std::sin(phi));

            Vec3 normal{static_cast<float>(-slope * cosP), static_cast<float>(-slope * sinP), 1.0f};
            normal = normal.normalized();

            GridVertex v{};
            v.posAndInfo = simd_make_float4(static_cast<float>(r) * cosP,
                                            static_cast<float>(r) * sinP,
                                            static_cast<float>(z),
                                            static_cast<float>(r));
            v.normalAndTwist = simd_make_float4(normal.x, normal.y, normal.z, twist);
            vertices.push_back(v);
        }
    }

    outIndices.clear();
    outIndices.reserve(static_cast<size_t>(totalRings - 1) * static_cast<size_t>(numSegs) * 6);
    int stride = numSegs + 1;
    for (int i = 0; i < totalRings - 1; ++i) {
        for (int j = 0; j < numSegs; ++j) {
            uint32_t a = static_cast<uint32_t>(i * stride + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * stride + j);
            uint32_t d = c + 1;
            outIndices.push_back(a);
            outIndices.push_back(c);
            outIndices.push_back(b);
            outIndices.push_back(b);
            outIndices.push_back(c);
            outIndices.push_back(d);
        }
    }

    return vertices;
}

bool saveBGRA8TextureToPNG(id<MTLTexture> texture, const std::string& path) {
    NSUInteger width = texture.width;
    NSUInteger height = texture.height;
    NSUInteger bytesPerRow = width * 4;
    std::vector<uint8_t> pixels(bytesPerRow * height);

    [texture getBytes:pixels.data()
          bytesPerRow:bytesPerRow
           fromRegion:MTLRegionMake2D(0, 0, width, height)
          mipmapLevel:0];

    // Swap BGRA -> RGBA for CGImage
    for (size_t i = 0; i < pixels.size(); i += 4) {
        std::swap(pixels[i], pixels[i + 2]);
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        pixels.data(), width, height, 8, bytesPerRow, colorSpace,
        static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CGImageRef cgImage = CGBitmapContextCreateImage(ctx);

    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)url,
        (__bridge CFStringRef)UTTypePNG.identifier,
        1, nullptr);

    bool ok = false;
    if (dest && cgImage) {
        CGImageDestinationAddImage(dest, cgImage, nullptr);
        ok = CGImageDestinationFinalize(dest);
    }

    if (dest) CFRelease(dest);
    if (cgImage) CGImageRelease(cgImage);
    if (ctx) CGContextRelease(ctx);
    if (colorSpace) CGColorSpaceRelease(colorSpace);
    return ok;
}

} // namespace

// ============================================================================
// Objective-C++ View & Renderer
// ============================================================================

@class BHController;

@interface BHMetalView : MTKView
@property (nonatomic, weak) BHController* controller;
@end

@interface BHRenderer : NSObject
@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLComputePipelineState> raytracePipeline;
@property (nonatomic, strong) id<MTLComputePipelineState> bloomExtractPipeline;
@property (nonatomic, strong) id<MTLRenderPipelineState> gridPipeline;
@property (nonatomic, strong) id<MTLRenderPipelineState> compositePipeline;

@property (nonatomic, strong) id<MTLTexture> hdrColorTex;
@property (nonatomic, strong) id<MTLTexture> hitDepthTex;
@property (nonatomic, strong) id<MTLTexture> bloomExtractTex;
@property (nonatomic, strong) id<MTLTexture> bloomTightTex;
@property (nonatomic, strong) id<MTLTexture> bloomWideTex;

@property (nonatomic, strong) MPSImageGaussianBlur* mpsBlurTight;
@property (nonatomic, strong) MPSImageGaussianBlur* mpsBlurWide;

@property (nonatomic, strong) id<MTLBuffer> gridVertexBuffer;
@property (nonatomic, strong) id<MTLBuffer> gridIndexBuffer;
@property (nonatomic, assign) NSUInteger gridIndexCount;

@property (nonatomic, assign) NSUInteger renderWidth;
@property (nonatomic, assign) NSUInteger renderHeight;
@property (nonatomic, assign) double cachedGridSpin;
@property (nonatomic, assign) float cachedGridOuterR;

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)ensureTexturesForWidth:(NSUInteger)w height:(NSUInteger)h;
- (void)ensureGridMeshForSpin:(double)spinA outerR:(float)outerR;
- (void)renderToDrawableTexture:(id<MTLTexture>)targetTex
                       drawable:(id<CAMetalDrawable>)drawable
                         params:(const SimulationParams&)params
                         camera:(const OrbitalCamera&)camera
                       syncBlit:(BOOL)syncBlit;
@end

@implementation BHRenderer

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (!self) return nil;

    _device = device;
    _commandQueue = [device newCommandQueue];
    _cachedGridSpin = -1.0;
    _cachedGridOuterR = -1.0f;

    NSError* error = nil;
    id<MTLLibrary> library = nil;

    // 1. Try precompiled default.metallib beside executable
    std::string exeDir = executableDir();
    std::string libPath = exeDir + "/default.metallib";
    if (std::filesystem::exists(libPath)) {
        NSURL* libURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:libPath.c_str()]];
        library = [device newLibraryWithURL:libURL error:&error];
    }

    // 2. Fallback: compile BlackHole.metal from source
    if (!library) {
        std::vector<std::string> candidates = {
            exeDir + "/../src/shaders/BlackHole.metal",
            exeDir + "/src/shaders/BlackHole.metal",
            "src/shaders/BlackHole.metal",
            "black-hole-cpp-hydrogen4/src/shaders/BlackHole.metal"
        };
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path)) {
                std::ifstream f(path);
                std::stringstream ss;
                ss << f.rdbuf();
                std::string src = ss.str();

                // Inline ShaderTypes.h include if compiling directly
                std::filesystem::path headerPath = std::filesystem::path(path).parent_path() / "ShaderTypes.h";
                if (std::filesystem::exists(headerPath)) {
                    std::ifstream hf(headerPath);
                    std::stringstream hss;
                    hss << hf.rdbuf();
                    std::string headerSrc = hss.str();
                    size_t pos = src.find("#include \"ShaderTypes.h\"");
                    if (pos != std::string::npos) {
                        src.replace(pos, std::string("#include \"ShaderTypes.h\"").size(), headerSrc);
                    }
                }

                MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
                opts.mathMode = MTLMathModeFast;
                library = [device newLibraryWithSource:[NSString stringWithUTF8String:src.c_str()]
                                               options:opts
                                                 error:&error];
                if (library) break;
            }
        }
    }

    if (!library) {
        NSLog(@"Failed to load Metal library: %@", error);
        return nil;
    }

    id<MTLFunction> raytraceFn = [library newFunctionWithName:@"raytraceBlackHole"];
    id<MTLFunction> bloomFn    = [library newFunctionWithName:@"extractBloomBright"];
    id<MTLFunction> gridVertFn = [library newFunctionWithName:@"vertexSpacetimeGrid"];
    id<MTLFunction> gridFragFn = [library newFunctionWithName:@"fragmentSpacetimeGrid"];
    id<MTLFunction> quadVertFn = [library newFunctionWithName:@"vertexFullscreenQuad"];
    id<MTLFunction> compFragFn = [library newFunctionWithName:@"fragmentCompositeFinal"];

    _raytracePipeline = [device newComputePipelineStateWithFunction:raytraceFn error:&error];
    if (!_raytracePipeline) { NSLog(@"raytracePipeline error: %@", error); return nil; }

    _bloomExtractPipeline = [device newComputePipelineStateWithFunction:bloomFn error:&error];
    if (!_bloomExtractPipeline) { NSLog(@"bloomExtractPipeline error: %@", error); return nil; }

    // Spacetime Grid pipeline (renders onto RGBA16Float HDR buffer with premultiplied alpha)
    MTLRenderPipelineDescriptor* gridDesc = [[MTLRenderPipelineDescriptor alloc] init];
    gridDesc.vertexFunction = gridVertFn;
    gridDesc.fragmentFunction = gridFragFn;
    gridDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
    gridDesc.colorAttachments[0].blendingEnabled = YES;
    gridDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    gridDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    gridDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    gridDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    gridDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    gridDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    _gridPipeline = [device newRenderPipelineStateWithDescriptor:gridDesc error:&error];
    if (!_gridPipeline) { NSLog(@"gridPipeline error: %@", error); return nil; }

    // Composite pipeline (outputs to BGRA8Unorm drawable)
    MTLRenderPipelineDescriptor* compDesc = [[MTLRenderPipelineDescriptor alloc] init];
    compDesc.vertexFunction = quadVertFn;
    compDesc.fragmentFunction = compFragFn;
    compDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    _compositePipeline = [device newRenderPipelineStateWithDescriptor:compDesc error:&error];
    if (!_compositePipeline) { NSLog(@"compositePipeline error: %@", error); return nil; }

    // MPS Gaussian Blur kernels for physical optical halos
    _mpsBlurTight = [[MPSImageGaussianBlur alloc] initWithDevice:device sigma:5.0f];
    _mpsBlurTight.edgeMode = MPSImageEdgeModeClamp;

    _mpsBlurWide = [[MPSImageGaussianBlur alloc] initWithDevice:device sigma:20.0f];
    _mpsBlurWide.edgeMode = MPSImageEdgeModeClamp;

    return self;
}

- (void)ensureTexturesForWidth:(NSUInteger)w height:(NSUInteger)h {
    w = std::max<NSUInteger>(w, 64);
    h = std::max<NSUInteger>(h, 64);
    if (_renderWidth == w && _renderHeight == h && _hdrColorTex != nil) return;

    _renderWidth = w;
    _renderHeight = h;

    MTLTextureDescriptor* hdrDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                       width:w
                                                                                      height:h
                                                                                   mipmapped:NO];
    hdrDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    hdrDesc.storageMode = MTLStorageModePrivate;
    _hdrColorTex = [_device newTextureWithDescriptor:hdrDesc];

    MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                                                         width:w
                                                                                        height:h
                                                                                     mipmapped:NO];
    depthDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    depthDesc.storageMode = MTLStorageModePrivate;
    _hitDepthTex = [_device newTextureWithDescriptor:depthDesc];

    NSUInteger bw = std::max<NSUInteger>(w / 2, 32);
    NSUInteger bh = std::max<NSUInteger>(h / 2, 32);
    MTLTextureDescriptor* bloomDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                         width:bw
                                                                                        height:bh
                                                                                     mipmapped:NO];
    bloomDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    bloomDesc.storageMode = MTLStorageModePrivate;
    _bloomExtractTex = [_device newTextureWithDescriptor:bloomDesc];
    _bloomTightTex   = [_device newTextureWithDescriptor:bloomDesc];
    _bloomWideTex    = [_device newTextureWithDescriptor:bloomDesc];
}

- (void)ensureGridMeshForSpin:(double)spinA outerR:(float)outerR {
    if (std::abs(spinA - _cachedGridSpin) < 1e-4 && std::abs(outerR - _cachedGridOuterR) < 1e-3 && _gridVertexBuffer != nil) {
        return;
    }
    _cachedGridSpin = spinA;
    _cachedGridOuterR = outerR;

    std::vector<uint32_t> indices;
    auto vertices = buildSpacetimeGridMesh(spinA, outerR, 128, 160, indices);
    _gridIndexCount = indices.size();

    _gridVertexBuffer = [_device newBufferWithBytes:vertices.data()
                                             length:vertices.size() * sizeof(GridVertex)
                                            options:MTLResourceStorageModeShared];
    _gridIndexBuffer = [_device newBufferWithBytes:indices.data()
                                            length:indices.size() * sizeof(uint32_t)
                                           options:MTLResourceStorageModeShared];
}

- (void)renderToDrawableTexture:(id<MTLTexture>)targetTex
                       drawable:(id<CAMetalDrawable>)drawable
                         params:(const SimulationParams&)params
                         camera:(const OrbitalCamera&)camera
                       syncBlit:(BOOL)syncBlit
{
    // Cap internal raytrace resolution to keep real-time 60+ fps on HiDPI Retina displays
    NSUInteger targetW = targetTex.width;
    NSUInteger targetH = targetTex.height;
    float scale = 1.0f;
    if (targetW > 1600) {
        scale = 1600.0f / static_cast<float>(targetW);
    }
    NSUInteger rayW = static_cast<NSUInteger>(std::max(320.0f, std::round(targetW * scale)));
    NSUInteger rayH = static_cast<NSUInteger>(std::max(240.0f, std::round(targetH * scale)));

    [self ensureTexturesForWidth:rayW height:rayH];
    [self ensureGridMeshForSpin:params.spinA outerR:params.gridOuterR];

    Vec3 camPos, camFwd, camRight, camUp;
    camera.computeBasis(camPos, camFwd, camRight, camUp);

    float aspect = static_cast<float>(rayW) / static_cast<float>(rayH);
    float fovRad = degToRad(camera.fovYDeg);
    float tanHalfFov = std::tan(0.5f * fovRad);

    Mat4 view = Mat4::lookAt(camPos, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
    Mat4 proj = Mat4::perspectiveMetal(fovRad, aspect, 0.25f, 250.0f);
    Mat4 vp = Mat4::multiply(proj, view);

    SimulationUniforms ubo{};
    ubo.camPosTime     = simd_make_float4(camPos.x, camPos.y, camPos.z, static_cast<float>(params.simTime));
    ubo.camForwardFov  = simd_make_float4(camFwd.x, camFwd.y, camFwd.z, tanHalfFov);
    ubo.camRightAspect = simd_make_float4(camRight.x, camRight.y, camRight.z, aspect);
    ubo.camUpDummy     = simd_make_float4(camUp.x, camUp.y, camUp.z, 0.0f);
    ubo.camBL          = simd_make_float4(camera.radius, camera.theta, camera.phi, std::cos(camera.theta));

    double rp = physics::eventHorizonRadius(params.spinA);
    double rISCO = physics::iscoRadius(params.spinA);
    ubo.bhParams = simd_make_float4(static_cast<float>(params.spinA),
                                    static_cast<float>(rp),
                                    static_cast<float>(rISCO),
                                    static_cast<float>(params.diskOuterR));
    ubo.diskParams = simd_make_float4(params.diskEnabled ? 1.0f : 0.0f,
                                      params.dopplerEnabled ? 1.0f : 0.0f,
                                      params.diskBrightness,
                                      params.haloIntensity);
    ubo.gridParams = simd_make_float4(params.gridEnabled ? 1.0f : 0.0f,
                                      params.gridVerticalOffset,
                                      params.gridOuterR,
                                      params.gridOpacity);
    ubo.visualParams = simd_make_float4(static_cast<float>(params.backgroundMode),
                                        params.bloomIntensity,
                                        params.infallParticlesEnabled ? 1.0f : 0.0f,
                                        params.exposure);
    ubo.screenParams = simd_make_float4(static_cast<float>(rayW),
                                        static_cast<float>(rayH),
                                        1.0f / static_cast<float>(rayW),
                                        1.0f / static_cast<float>(rayH));
    memcpy(&ubo.viewProj, vp.m.data(), sizeof(simd_float4x4));

    id<MTLCommandBuffer> cmd = [_commandQueue commandBuffer];

    // 1. Kerr Geodesic Raytrace Compute Pass
    {
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:_raytracePipeline];
        [ce setTexture:_hdrColorTex atIndex:0];
        [ce setTexture:_hitDepthTex atIndex:1];
        [ce setBytes:&ubo length:sizeof(ubo) atIndex:0];

        MTLSize tgSize = MTLSizeMake(8, 8, 1);
        MTLSize gridSize = MTLSizeMake(rayW, rayH, 1);
        [ce dispatchThreads:gridSize threadsPerThreadgroup:tgSize];
        [ce endEncoding];
    }

    // 2. 3D Spacetime Curvature ("Trapdoor") Grid Render Pass into HDR buffer
    if (params.gridEnabled && _gridIndexCount > 0) {
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = _hdrColorTex;
        rpd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> re = [cmd renderCommandEncoderWithDescriptor:rpd];
        [re setRenderPipelineState:_gridPipeline];
        [re setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(rayW), static_cast<double>(rayH), 0.0, 1.0}];
        [re setVertexBuffer:_gridVertexBuffer offset:0 atIndex:0];
        [re setVertexBytes:&ubo length:sizeof(ubo) atIndex:1];
        [re setFragmentTexture:_hitDepthTex atIndex:0];
        [re setFragmentBytes:&ubo length:sizeof(ubo) atIndex:0];
        [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:_gridIndexCount
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:_gridIndexBuffer
                indexBufferOffset:0];
        [re endEncoding];
    }

    // 3. MPS-Accelerated Relativistic Bloom & Halos
    if (params.bloomIntensity > 0.005f) {
        id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
        [ce setComputePipelineState:_bloomExtractPipeline];
        [ce setTexture:_hdrColorTex atIndex:0];
        [ce setTexture:_bloomExtractTex atIndex:1];
        MTLSize tgSize = MTLSizeMake(8, 8, 1);
        MTLSize gridSize = MTLSizeMake(_bloomExtractTex.width, _bloomExtractTex.height, 1);
        [ce dispatchThreads:gridSize threadsPerThreadgroup:tgSize];
        [ce endEncoding];

        [_mpsBlurTight encodeToCommandBuffer:cmd sourceTexture:_bloomExtractTex destinationTexture:_bloomTightTex];
        [_mpsBlurWide  encodeToCommandBuffer:cmd sourceTexture:_bloomExtractTex destinationTexture:_bloomWideTex];
    }

    // 4. Final ACES Filmic Composite to Target Drawable
    {
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = targetTex;
        rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> re = [cmd renderCommandEncoderWithDescriptor:rpd];
        [re setRenderPipelineState:_compositePipeline];
        [re setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(targetW), static_cast<double>(targetH), 0.0, 1.0}];
        [re setFragmentTexture:_hdrColorTex atIndex:0];
        [re setFragmentTexture:_bloomTightTex atIndex:1];
        [re setFragmentTexture:_bloomWideTex atIndex:2];
        [re setFragmentBytes:&ubo length:sizeof(ubo) atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    }

    if (drawable != nil) {
        [cmd presentDrawable:drawable];
    }
    [cmd commit];
    if (syncBlit) {
        [cmd waitUntilCompleted];
    }
}

@end

// ============================================================================
// Controller, Window, Input & Native Translucent HUD
// ============================================================================

@interface BHController : NSObject <NSWindowDelegate, MTKViewDelegate> {
    SimulationParams _params;
    OrbitalCamera _camera;
    std::chrono::steady_clock::time_point _lastFrameTime;
    double _fpsSmoothed;
    double _lastTelemetryUpdate;
    NSPoint _lastMousePt;
    BOOL _isDragging;
}
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) BHMetalView* mtkView;
@property (nonatomic, strong) BHRenderer* renderer;
@property (nonatomic, strong) NSVisualEffectView* hudPanel;

// UI controls
@property (nonatomic, strong) NSSlider* spinSlider;
@property (nonatomic, strong) NSTextField* spinValueLabel;
@property (nonatomic, strong) NSSlider* brightnessSlider;
@property (nonatomic, strong) NSSlider* haloSlider;
@property (nonatomic, strong) NSSlider* gridOffsetSlider;
@property (nonatomic, strong) NSSlider* gridOpacitySlider;

@property (nonatomic, strong) NSButton* diskCheck;
@property (nonatomic, strong) NSButton* dopplerCheck;
@property (nonatomic, strong) NSButton* gridCheck;
@property (nonatomic, strong) NSButton* infallCheck;
@property (nonatomic, strong) NSButton* pauseBtn;
@property (nonatomic, strong) NSSegmentedControl* bgSegment;
@property (nonatomic, strong) NSTextField* telemetryLabel;

- (instancetype)initWithOptions:(const LaunchOptions&)opts;
- (void)setupWindowAndHUD;
- (void)handleMouseDraggedDelta:(NSPoint)delta;
- (void)handleScrollDeltaY:(CGFloat)deltaY;
- (void)handleMagnify:(CGFloat)magnification;
- (BOOL)handleKeyCode:(unsigned short)keyCode chars:(NSString*)chars;
@end

@implementation BHMetalView

- (BOOL)acceptsFirstResponder { return YES; }

- (void)mouseDown:(NSEvent*)event {
    [[self window] makeFirstResponder:self];
    [self.controller performSelector:@selector(beginDrag:) withObject:event];
}

- (void)rightMouseDown:(NSEvent*)event {
    [self.controller performSelector:@selector(beginDrag:) withObject:event];
}

- (void)mouseDragged:(NSEvent*)event {
    [self.controller handleMouseDraggedDelta:NSMakePoint(event.deltaX, event.deltaY)];
}

- (void)rightMouseDragged:(NSEvent*)event {
    [self.controller handleMouseDraggedDelta:NSMakePoint(event.deltaX, event.deltaY)];
}

- (void)scrollWheel:(NSEvent*)event {
    [self.controller handleScrollDeltaY:event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.08 : 0.8)];
}

- (void)magnifyWithEvent:(NSEvent*)event {
    [self.controller handleMagnify:event.magnification];
}

- (void)keyDown:(NSEvent*)event {
    if (![self.controller handleKeyCode:event.keyCode chars:event.charactersIgnoringModifiers]) {
        [super keyDown:event];
    }
}

@end

@implementation BHController

- (instancetype)initWithOptions:(const LaunchOptions&)opts {
    self = [super init];
    if (!self) return nil;

    _params.spinA = physics::clampSpin(opts.captureSpin);
    _params.gridEnabled = opts.captureGrid;
    _params.diskEnabled = opts.captureDisk;
    _camera.applyPreset(static_cast<CameraPreset>(opts.capturePreset));
    _camera.snapToTarget();
    _lastFrameTime = std::chrono::steady_clock::now();
    _fpsSmoothed = 60.0;
    _lastTelemetryUpdate = 0.0;
    return self;
}

- (void)beginDrag:(NSEvent*)event {
    _lastMousePt = event.locationInWindow;
    _isDragging = YES;
}

- (void)handleMouseDraggedDelta:(NSPoint)delta {
    _camera.orbit(static_cast<float>(delta.x) * 0.0065f, static_cast<float>(delta.y) * 0.0065f);
}

- (void)handleScrollDeltaY:(CGFloat)deltaY {
    _camera.zoom(static_cast<float>(deltaY));
}

- (void)handleMagnify:(CGFloat)magnification {
    _camera.zoom(static_cast<float>(magnification) * 8.0f);
}

- (BOOL)handleKeyCode:(unsigned short)keyCode chars:(NSString*)chars {
    if (keyCode == 49) { // Space
        _params.paused = !_params.paused;
        [self syncUIFromState];
        return YES;
    }
    if (keyCode == 53) { // Escape
        [NSApp terminate:nil];
        return YES;
    }
    NSString* lower = [chars lowercaseString];
    if ([lower isEqualToString:@"1"]) { _camera.applyPreset(CameraPreset::GargantuaInclined); return YES; }
    if ([lower isEqualToString:@"2"]) { _camera.applyPreset(CameraPreset::EdgeOnLensing); return YES; }
    if ([lower isEqualToString:@"3"]) { _camera.applyPreset(CameraPreset::PolarTopDown); return YES; }
    if ([lower isEqualToString:@"4"]) { _camera.applyPreset(CameraPreset::ClosePhotonSkim); return YES; }
    if ([lower isEqualToString:@"g"]) { _params.gridEnabled = !_params.gridEnabled; [self syncUIFromState]; return YES; }
    if ([lower isEqualToString:@"d"]) { _params.diskEnabled = !_params.diskEnabled; [self syncUIFromState]; return YES; }
    if ([lower isEqualToString:@"b"]) { _params.backgroundMode = 1 - _params.backgroundMode; [self syncUIFromState]; return YES; }
    if ([lower isEqualToString:@"h"]) { self.hudPanel.hidden = !self.hudPanel.hidden; return YES; }
    if ([lower isEqualToString:@"r"]) {
        _params.resetDefaults();
        _camera.applyPreset(CameraPreset::GargantuaInclined);
        [self syncUIFromState];
        return YES;
    }
    return NO;
}

- (NSTextField*)makeLabel:(NSString*)text size:(CGFloat)size bold:(BOOL)bold color:(NSColor*)color {
    NSTextField* lbl = [NSTextField labelWithString:text];
    lbl.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    lbl.textColor = color ?: [NSColor labelColor];
    lbl.translatesAutoresizingMaskIntoConstraints = NO;
    return lbl;
}

- (NSSlider*)makeSliderMin:(double)min max:(double)max val:(double)val action:(SEL)sel {
    NSSlider* s = [NSSlider sliderWithValue:val minValue:min maxValue:max target:self action:sel];
    s.translatesAutoresizingMaskIntoConstraints = NO;
    s.continuous = YES;
    return s;
}

- (NSButton*)makeCheckbox:(NSString*)title checked:(BOOL)checked action:(SEL)sel {
    NSButton* b = [NSButton checkboxWithTitle:title target:self action:sel];
    b.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    b.font = [NSFont systemFontOfSize:12];
    return b;
}

- (void)setupWindowAndHUD {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    _renderer = [[BHRenderer alloc] initWithDevice:device];

    NSRect frame = NSMakeRect(0, 0, 1440, 900);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Kerr Black Hole — General Relativistic Raytracer & Spacetime Curvature (Metal + MPS)";
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _window.backgroundColor = [NSColor blackColor];
    _window.delegate = self;
    [_window center];

    _mtkView = [[BHMetalView alloc] initWithFrame:frame device:device];
    _mtkView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    _mtkView.framebufferOnly = YES;
    _mtkView.preferredFramesPerSecond = 60;
    _mtkView.delegate = self;
    _mtkView.controller = self;
    _mtkView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _window.contentView = _mtkView;

    // Translucent HUD Panel on right side
    _hudPanel = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, 290, 740)];
    _hudPanel.material = NSVisualEffectMaterialHUDWindow;
    _hudPanel.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    _hudPanel.state = NSVisualEffectStateActive;
    _hudPanel.wantsLayer = YES;
    _hudPanel.layer.cornerRadius = 14.0;
    _hudPanel.layer.borderWidth = 1.0;
    _hudPanel.layer.borderColor = [[NSColor colorWithWhite:1.0 alpha:0.14] CGColor];
    _hudPanel.translatesAutoresizingMaskIntoConstraints = NO;
    [_mtkView addSubview:_hudPanel];

    [NSLayoutConstraint activateConstraints:@[
        [_hudPanel.topAnchor constraintEqualToAnchor:_mtkView.topAnchor constant:16],
        [_hudPanel.trailingAnchor constraintEqualToAnchor:_mtkView.trailingAnchor constant:-16],
        [_hudPanel.widthAnchor constraintEqualToConstant:296]
    ]];

    NSStackView* stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [_hudPanel addSubview:stack];

    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:_hudPanel.topAnchor constant:14],
        [stack.leadingAnchor constraintEqualToAnchor:_hudPanel.leadingAnchor constant:14],
        [stack.trailingAnchor constraintEqualToAnchor:_hudPanel.trailingAnchor constant:-14],
        [stack.bottomAnchor constraintEqualToAnchor:_hudPanel.bottomAnchor constant:-14]
    ]];

    NSTextField* title = [self makeLabel:@"KERR BLACK HOLE SIM" size:13 bold:YES color:[NSColor whiteColor]];
    NSTextField* sub = [self makeLabel:@"Drag: Orbit • Scroll: Zoom • H: Hide HUD" size:10.5 bold:NO color:[NSColor secondaryLabelColor]];
    [stack addArrangedSubview:title];
    [stack addArrangedSubview:sub];

    // Spin slider
    _spinValueLabel = [self makeLabel:@"Spin a/M: 0.900" size:11.5 bold:YES color:[NSColor systemCyanColor]];
    _spinSlider = [self makeSliderMin:0.0 max:0.998 val:_params.spinA action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_spinValueLabel];
    [stack addArrangedSubview:_spinSlider];
    [_spinSlider.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    // Toggles
    _diskCheck    = [self makeCheckbox:@"Accretion Disk (Novikov-Thorne)" checked:_params.diskEnabled action:@selector(onControlChanged:)];
    _dopplerCheck = [self makeCheckbox:@"Relativistic Doppler & Redshift" checked:_params.dopplerEnabled action:@selector(onControlChanged:)];
    _gridCheck    = [self makeCheckbox:@"Spacetime Curvature Trapdoor Grid (G)" checked:_params.gridEnabled action:@selector(onControlChanged:)];
    _infallCheck  = [self makeCheckbox:@"Infalling Geodesic Waves" checked:_params.infallParticlesEnabled action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_diskCheck];
    [stack addArrangedSubview:_dopplerCheck];
    [stack addArrangedSubview:_gridCheck];
    [stack addArrangedSubview:_infallCheck];

    // Brightness / Halo / Grid sliders
    [stack addArrangedSubview:[self makeLabel:@"Accretion Disk Luminosity" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    _brightnessSlider = [self makeSliderMin:0.2 max:3.0 val:_params.diskBrightness action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_brightnessSlider];
    [_brightnessSlider.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    [stack addArrangedSubview:[self makeLabel:@"Photon Ring & MPS Corona Halo" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    _haloSlider = [self makeSliderMin:0.0 max:2.5 val:_params.haloIntensity action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_haloSlider];
    [_haloSlider.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    [stack addArrangedSubview:[self makeLabel:@"Trapdoor Grid Vertical Offset" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    _gridOffsetSlider = [self makeSliderMin:-7.0 max:0.5 val:_params.gridVerticalOffset action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_gridOffsetSlider];
    [_gridOffsetSlider.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    [stack addArrangedSubview:[self makeLabel:@"Trapdoor Grid Opacity" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    _gridOpacitySlider = [self makeSliderMin:0.1 max:1.0 val:_params.gridOpacity action:@selector(onControlChanged:)];
    [stack addArrangedSubview:_gridOpacitySlider];
    [_gridOpacitySlider.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    // Lensing Background Mode
    [stack addArrangedSubview:[self makeLabel:@"Gravitational Lensing Background (B)" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    _bgSegment = [NSSegmentedControl segmentedControlWithLabels:@[@"Deep Space", @"Einstein Grid"]
                                                   trackingMode:NSSegmentSwitchTrackingSelectOne
                                                         target:self
                                                         action:@selector(onControlChanged:)];
    _bgSegment.selectedSegment = _params.backgroundMode;
    _bgSegment.translatesAutoresizingMaskIntoConstraints = NO;
    [stack addArrangedSubview:_bgSegment];
    [_bgSegment.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    // Camera Presets
    [stack addArrangedSubview:[self makeLabel:@"Camera Presets (Keys 1-4)" size:11 bold:NO color:[NSColor secondaryLabelColor]]];
    NSStackView* row1 = [[NSStackView alloc] init];
    row1.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row1.distribution = NSStackViewDistributionFillEqually;
    row1.spacing = 6;
    NSButton* p1 = [NSButton buttonWithTitle:@"1 Inclined" target:self action:@selector(onPreset1:)];
    NSButton* p2 = [NSButton buttonWithTitle:@"2 Edge-On" target:self action:@selector(onPreset2:)];
    p1.bezelStyle = NSBezelStyleRounded; p2.bezelStyle = NSBezelStyleRounded;
    [row1 addArrangedSubview:p1]; [row1 addArrangedSubview:p2];
    [stack addArrangedSubview:row1];
    [row1.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    NSStackView* row2 = [[NSStackView alloc] init];
    row2.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row2.distribution = NSStackViewDistributionFillEqually;
    row2.spacing = 6;
    NSButton* p3 = [NSButton buttonWithTitle:@"3 Polar" target:self action:@selector(onPreset3:)];
    NSButton* p4 = [NSButton buttonWithTitle:@"4 Photon Skim" target:self action:@selector(onPreset4:)];
    p3.bezelStyle = NSBezelStyleRounded; p4.bezelStyle = NSBezelStyleRounded;
    [row2 addArrangedSubview:p3]; [row2 addArrangedSubview:p4];
    [stack addArrangedSubview:row2];
    [row2.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    NSStackView* row3 = [[NSStackView alloc] init];
    row3.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row3.distribution = NSStackViewDistributionFillEqually;
    row3.spacing = 6;
    _pauseBtn = [NSButton buttonWithTitle:@"Pause (Space)" target:self action:@selector(onTogglePause:)];
    NSButton* resetBtn = [NSButton buttonWithTitle:@"Reset (R)" target:self action:@selector(onReset:)];
    _pauseBtn.bezelStyle = NSBezelStyleRounded; resetBtn.bezelStyle = NSBezelStyleRounded;
    [row3 addArrangedSubview:_pauseBtn]; [row3 addArrangedSubview:resetBtn];
    [stack addArrangedSubview:row3];
    [row3.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    // Telemetry Readout
    _telemetryLabel = [NSTextField wrappingLabelWithString:@""];
    _telemetryLabel.font = [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    _telemetryLabel.textColor = [NSColor colorWithWhite:0.86 alpha:1.0];
    _telemetryLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [stack addArrangedSubview:_telemetryLabel];
    [_telemetryLabel.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    [self updateTelemetryText];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_mtkView];
}

- (void)syncUIFromState {
    _spinSlider.doubleValue = _params.spinA;
    _diskCheck.state = _params.diskEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    _dopplerCheck.state = _params.dopplerEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    _gridCheck.state = _params.gridEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    _infallCheck.state = _params.infallParticlesEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    _brightnessSlider.doubleValue = _params.diskBrightness;
    _haloSlider.doubleValue = _params.haloIntensity;
    _gridOffsetSlider.doubleValue = _params.gridVerticalOffset;
    _gridOpacitySlider.doubleValue = _params.gridOpacity;
    _bgSegment.selectedSegment = _params.backgroundMode;
    _pauseBtn.title = _params.paused ? @"Resume (Space)" : @"Pause (Space)";
    [self updateTelemetryText];
}

- (void)onControlChanged:(id)sender {
    (void)sender;
    _params.spinA = physics::clampSpin(_spinSlider.doubleValue);
    _params.diskEnabled = (_diskCheck.state == NSControlStateValueOn);
    _params.dopplerEnabled = (_dopplerCheck.state == NSControlStateValueOn);
    _params.gridEnabled = (_gridCheck.state == NSControlStateValueOn);
    _params.infallParticlesEnabled = (_infallCheck.state == NSControlStateValueOn);
    _params.diskBrightness = static_cast<float>(_brightnessSlider.doubleValue);
    _params.haloIntensity = static_cast<float>(_haloSlider.doubleValue);
    _params.bloomIntensity = 0.48f * (_params.haloIntensity / 1.15f);
    _params.gridVerticalOffset = static_cast<float>(_gridOffsetSlider.doubleValue);
    _params.gridOpacity = static_cast<float>(_gridOpacitySlider.doubleValue);
    _params.backgroundMode = static_cast<int>(_bgSegment.selectedSegment);
    [self updateTelemetryText];
}

- (void)onPreset1:(id)sender { (void)sender; _camera.applyPreset(CameraPreset::GargantuaInclined); [_window makeFirstResponder:_mtkView]; }
- (void)onPreset2:(id)sender { (void)sender; _camera.applyPreset(CameraPreset::EdgeOnLensing); [_window makeFirstResponder:_mtkView]; }
- (void)onPreset3:(id)sender { (void)sender; _camera.applyPreset(CameraPreset::PolarTopDown); [_window makeFirstResponder:_mtkView]; }
- (void)onPreset4:(id)sender { (void)sender; _camera.applyPreset(CameraPreset::ClosePhotonSkim); [_window makeFirstResponder:_mtkView]; }
- (void)onTogglePause:(id)sender { (void)sender; _params.paused = !_params.paused; [self syncUIFromState]; [_window makeFirstResponder:_mtkView]; }
- (void)onReset:(id)sender {
    (void)sender;
    _params.resetDefaults();
    _camera.applyPreset(CameraPreset::GargantuaInclined);
    [self syncUIFromState];
    [_window makeFirstResponder:_mtkView];
}

- (void)updateTelemetryText {
    double rp = physics::eventHorizonRadius(_params.spinA);
    double rISCO = physics::iscoRadius(_params.spinA);
    double rPhPro = physics::photonSpherePrograde(_params.spinA);
    double rPhRet = physics::photonSphereRetrograde(_params.spinA);
    double omegaH = physics::horizonAngularVelocity(_params.spinA);
    float incDeg = radToDeg(_camera.theta);

    _spinValueLabel.stringValue = [NSString stringWithFormat:@"Spin a/M: %.3f (%s)",
        _params.spinA, (_params.spinA < 0.02 ? "Schwarzschild" : "Kerr")];

    _telemetryLabel.stringValue = [NSString stringWithFormat:
        @"FPS: %.1f (%.1f ms) | Metal + MPS\n"
        @"r_+ (Horizon): %.3f M\n"
        @"r_ISCO:        %.3f M\n"
        @"r_ph-/r_ph+:   %.2f / %.2f M\n"
        @"Omega_H:       %.3f c/M\n"
        @"Camera: r=%.1f M, inc=%.1f°",
        _fpsSmoothed, 1000.0 / std::max(1.0, _fpsSmoothed),
        rp, rISCO, rPhPro, rPhRet, omegaH,
        _camera.radius, incDeg];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view; (void)size;
}

- (void)drawInMTKView:(MTKView*)view {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - _lastFrameTime).count();
    _lastFrameTime = now;
    dt = std::clamp(dt, 0.001f, 0.1f);

    double instFps = 1.0 / static_cast<double>(dt);
    _fpsSmoothed = 0.90 * _fpsSmoothed + 0.10 * instFps;

    if (!_params.paused) {
        _params.simTime += static_cast<double>(dt) * _params.timeScale;
    }
    _camera.update(dt);

    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!drawable) return;

    [_renderer renderToDrawableTexture:drawable.texture drawable:drawable params:_params camera:_camera syncBlit:NO];

    if (_params.simTime - _lastTelemetryUpdate > 0.12 || _params.paused) {
        _lastTelemetryUpdate = _params.simTime;
        [self updateTelemetryText];
    }
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [NSApp terminate:nil];
}

@end

// ============================================================================
// C++ Entry Point
// ============================================================================

namespace bh {

int runMetalApplication(const LaunchOptions& opts) {
    @autoreleasepool {
        if (!opts.capturePath.empty()) {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) {
                fprintf(stderr, "Error: Metal device unavailable.\n");
                return 1;
            }
            BHRenderer* renderer = [[BHRenderer alloc] initWithDevice:device];
            if (!renderer) {
                fprintf(stderr, "Error: Failed to initialize Metal renderer.\n");
                return 1;
            }

            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                            width:opts.width
                                                                                           height:opts.height
                                                                                        mipmapped:NO];
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeShared;
            id<MTLTexture> targetTex = [device newTextureWithDescriptor:desc];

            SimulationParams params;
            params.spinA = physics::clampSpin(opts.captureSpin);
            params.gridEnabled = opts.captureGrid;
            params.diskEnabled = opts.captureDisk;
            params.simTime = 4.2;

            OrbitalCamera cam;
            cam.applyPreset(static_cast<CameraPreset>(opts.capturePreset));
            cam.snapToTarget();

            [renderer renderToDrawableTexture:targetTex drawable:nil params:params camera:cam syncBlit:YES];
            bool saved = saveBGRA8TextureToPNG(targetTex, opts.capturePath);
            if (!saved) {
                fprintf(stderr, "Error: Failed to write capture PNG to %s\n", opts.capturePath.c_str());
                return 1;
            }
            return 0;
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        BHController* controller = [[BHController alloc] initWithOptions:opts];
        [controller setupWindowAndHUD];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

} // namespace bh
