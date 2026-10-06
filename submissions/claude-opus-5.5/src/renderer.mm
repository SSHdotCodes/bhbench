// renderer.mm — Metal pipelines, textures and pass encoding.
#import "renderer.h"

#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>

static constexpr int kBloomLevels = 6;

id<MTLComputePipelineState> Renderer::compute(NSString* name, std::string& err) {
    id<MTLFunction> f = [lib_ newFunctionWithName:name];
    if (!f) {
        err = std::string("missing Metal function ") + name.UTF8String;
        return nil;
    }
    NSError* e = nil;
    id<MTLComputePipelineState> p = [dev_ newComputePipelineStateWithFunction:f error:&e];
    if (!p) err = std::string("pipeline ") + name.UTF8String + ": " + e.localizedDescription.UTF8String;
    return p;
}

bool Renderer::init(id<MTLDevice> device, const std::string& metallibPath, MTLPixelFormat outputFormat, std::string& err) {
    dev_ = device;
    outFormat_ = outputFormat;
    queue_ = [dev_ newCommandQueue];
    NSError* e = nil;
    lib_ = [dev_ newLibraryWithURL:[NSURL fileURLWithPath:@(metallibPath.c_str())] error:&e];
    if (!lib_) {
        err = "cannot load " + metallibPath + ": " + (e ? e.localizedDescription.UTF8String : "?");
        return false;
    }
    MTLRenderPipelineDescriptor* rd = [MTLRenderPipelineDescriptor new];
    rd.vertexFunction = [lib_ newFunctionWithName:@"fullscreenVertex"];
    rd.fragmentFunction = [lib_ newFunctionWithName:@"traceFragment"];
    rd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
    tracePSO_ = [dev_ newRenderPipelineStateWithDescriptor:rd error:&e];
    if (!tracePSO_) {
        err = std::string("trace pipeline: ") + e.localizedDescription.UTF8String;
        return false;
    }
    rd.fragmentFunction = [lib_ newFunctionWithName:@"compositeFragment"];
    rd.colorAttachments[0].pixelFormat = outputFormat;
    compositePSO_ = [dev_ newRenderPipelineStateWithDescriptor:rd error:&e];
    if (!compositePSO_) {
        err = std::string("composite pipeline: ") + e.localizedDescription.UTF8String;
        return false;
    }
    accumPSO_ = compute(@"accumulate", err);
    downPSO_ = compute(@"bloomDown", err);
    upPSO_ = compute(@"bloomUp", err);
    skyPSO_ = compute(@"skyGenerate", err);
    cubeWritePSO_ = compute(@"cubeWriteTest", err);
    cubeReadPSO_ = compute(@"cubeReadTest", err);
    if (!accumPSO_ || !downPSO_ || !upPSO_ || !skyPSO_ || !cubeWritePSO_ || !cubeReadPSO_) return false;

    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                  width:1 height:1 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    dummy_ = [dev_ newTextureWithDescriptor:td];
    return true;
}

void Renderer::syncTables(const SceneCache& c) {
    if (c.version == tableVersion_) return;
    tableVersion_ = c.version;
    flux_ = [dev_ newBufferWithBytes:c.disk.flux.data() length:c.disk.flux.size() * sizeof(float)
                             options:MTLResourceStorageModeShared];
    lut_ = [dev_ newBufferWithBytes:c.lut.data() length:c.lut.size() * sizeof(simd_float4)
                            options:MTLResourceStorageModeShared];
    funnel_ = [dev_ newBufferWithBytes:c.funnel.Z.data() length:c.funnel.Z.size() * sizeof(float)
                               options:MTLResourceStorageModeShared];
}

static simd_float3 nrm(simd_float3 v) { return simd_normalize(v); }

void Renderer::generateSky(int N) {
    MTLTextureDescriptor* td = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                                                                     size:N mipmapped:YES];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    td.storageMode = MTLStorageModePrivate;
    sky_ = [dev_ newTextureWithDescriptor:td];

    SkyParams P;
    memset(&P, 0, sizeof P);
    // Galactic frame: the galactic centre lies behind the hole as seen from the default camera (+x axis),
    // and the Milky Way's plane is tilted with respect to the black hole's equator.
    simd_float3 gc = nrm(simd_make_float3(-1.0f, -0.22f, 0.16f));
    simd_float3 up = simd_make_float3(0.45f, 0.35f, 1.0f);
    simd_float3 gz = nrm(up - simd_dot(up, gc) * gc);
    simd_float3 gy = simd_cross(gz, gc);
    P.galX = gc;
    P.galY = gy;
    P.galZ = gz;
    P.starGrid = 0.19f * N;
    P.starProb = 0.028f;
    P.starFlux = 0.03f;
    P.milkyWay = 0.006f;
    P.size = N;

    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:skyPSO_];
    [ce setTexture:sky_ atIndex:0];
    [ce setBytes:&P length:sizeof P atIndex:0];
    [ce dispatchThreads:MTLSizeMake(N, N, 6) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [ce endEncoding];
    id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be generateMipmapsForTexture:sky_];
    [be endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
}

void Renderer::ensureTraceSize(int w, int h) {
    w = std::max(w, 8);
    h = std::max(h, 8);
    if (w == traceW_ && h == traceH_) return;
    traceW_ = w;
    traceH_ = h;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                  width:w height:h mipmapped:NO];
    td.storageMode = MTLStorageModePrivate;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    traceTex_ = [dev_ newTextureWithDescriptor:td];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    accumTex_ = [dev_ newTextureWithDescriptor:td];
    down_.clear();
    up_.clear();
    for (int i = 0; i < kBloomLevels; ++i) {
        int bw = std::max(1, w >> (i + 1)), bh = std::max(1, h >> (i + 1));
        MTLTextureDescriptor* bd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                      width:bw height:bh mipmapped:NO];
        bd.storageMode = MTLStorageModePrivate;
        bd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        down_.push_back([dev_ newTextureWithDescriptor:bd]);
        up_.push_back([dev_ newTextureWithDescriptor:bd]);
    }
}

static void dispatch2D(id<MTLComputeCommandEncoder> ce, id<MTLTexture> t) {
    [ce dispatchThreads:MTLSizeMake(t.width, t.height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
}

void Renderer::encodeFrame(id<MTLCommandBuffer> cb, const FrameUniforms& U, bool doTrace, float accumWeight,
                           const PostUniforms& P, id<MTLTexture> target) {
    if (doTrace) {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = traceTex_;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
        re.label = @"trace";
        [re setRenderPipelineState:tracePSO_];
        [re setFragmentBytes:&U length:sizeof U atIndex:0];
        [re setFragmentBuffer:flux_ offset:0 atIndex:1];
        [re setFragmentBuffer:lut_ offset:0 atIndex:2];
        [re setFragmentBuffer:funnel_ offset:0 atIndex:3];
        [re setFragmentTexture:sky_ atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    }
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    if (doTrace) {
        [ce setComputePipelineState:accumPSO_];
        [ce setTexture:traceTex_ atIndex:0];
        [ce setTexture:accumTex_ atIndex:1];
        [ce setBytes:&accumWeight length:sizeof(float) atIndex:0];
        dispatch2D(ce, accumTex_);
    }
    if (P.bloomOn) {
        [ce setComputePipelineState:downPSO_];
        for (int i = 0; i < kBloomLevels; ++i) {
            [ce setTexture:(i == 0 ? accumTex_ : down_[i - 1]) atIndex:0];
            [ce setTexture:down_[i] atIndex:1];
            dispatch2D(ce, down_[i]);
        }
        [ce setComputePipelineState:upPSO_];
        for (int i = kBloomLevels - 2; i >= 0; --i) {
            [ce setTexture:(i == kBloomLevels - 2 ? down_[i + 1] : up_[i + 1]) atIndex:0];
            [ce setTexture:down_[i] atIndex:1];
            [ce setTexture:up_[i] atIndex:2];
            dispatch2D(ce, up_[i]);
        }
    }
    [ce endEncoding];

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    re.label = @"composite";
    [re setRenderPipelineState:compositePSO_];
    [re setFragmentTexture:accumTex_ atIndex:0];
    [re setFragmentTexture:(P.bloomOn ? up_[0] : dummy_) atIndex:1];
    [re setFragmentBytes:&P length:sizeof P atIndex:0];
    [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [re endEncoding];
}

std::vector<simd_float4> Renderer::traceDebug(const FrameUniforms& Uin, int w, int h) {
    FrameUniforms U = Uin;
    U.flags |= BH_FLAG_DEBUG;
    U.resolution = simd_make_float2(float(w), float(h));
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                  width:w height:h mipmapped:NO];
    td.storageMode = MTLStorageModePrivate;
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> tex = [dev_ newTextureWithDescriptor:td];
    id<MTLBuffer> buf = [dev_ newBufferWithLength:size_t(w) * h * 16 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = tex;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:tracePSO_];
    [re setFragmentBytes:&U length:sizeof U atIndex:0];
    [re setFragmentBuffer:flux_ offset:0 atIndex:1];
    [re setFragmentBuffer:lut_ offset:0 atIndex:2];
    [re setFragmentBuffer:funnel_ offset:0 atIndex:3];
    [re setFragmentTexture:sky_ atIndex:0];
    [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [re endEncoding];
    id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:size_t(w) * 16
    destinationBytesPerImage:size_t(w) * h * 16];
    [be endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    std::vector<simd_float4> out(size_t(w) * h);
    memcpy(out.data(), buf.contents, out.size() * 16);
    return out;
}

bool Renderer::cubeConventionTest(std::string& report) {
    const int N = 8;
    MTLTextureDescriptor* td = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRG32Float
                                                                                     size:N mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> cube = [dev_ newTextureWithDescriptor:td];
    id<MTLBuffer> buf = [dev_ newBufferWithLength:6 * N * N * sizeof(simd_float2) options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:cubeWritePSO_];
    [ce setTexture:cube atIndex:0];
    [ce dispatchThreads:MTLSizeMake(N, N, 6) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [ce setComputePipelineState:cubeReadPSO_];
    [ce setTexture:cube atIndex:0];
    [ce setBuffer:buf offset:0 atIndex:0];
    [ce dispatchThreads:MTLSizeMake(N, N, 6) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    const simd_float2* r = (const simd_float2*)buf.contents;
    int bad = 0;
    for (int f = 0; f < 6; ++f)
        for (int i = 0; i < N * N; ++i) {
            simd_float2 v = r[f * N * N + i];
            if (int(v.x) != f || int(v.y) != i) ++bad;
        }
    report = bad == 0 ? "cube-map face convention: all 384 texels round-trip"
                      : "cube-map face convention: " + std::to_string(bad) + "/384 texels mismatch";
    return bad == 0;
}

bool writePNG(const std::string& path, const uint8_t* bgra, int w, int h, int bytesPerRow) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, bgra, size_t(bytesPerRow) * h, nullptr);
    CGImageRef img = CGImageCreate(w, h, 8, 32, bytesPerRow, cs,
                                   kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst, dp, nullptr,
                                   false, kCGRenderingIntentDefault);
    NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
    CGImageDestinationRef dst =
        CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    bool ok = false;
    if (dst) {
        CGImageDestinationAddImage(dst, img, nullptr);
        ok = CGImageDestinationFinalize(dst);
        CFRelease(dst);
    }
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
    return ok;
}
