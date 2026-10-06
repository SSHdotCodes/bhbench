// renderer.h — Metal resources and per-frame pass encoding (Objective-C++ only).
#pragma once
#import <Metal/Metal.h>

#include <string>
#include <vector>

#include "scene.h"

class Renderer {
public:
    bool init(id<MTLDevice> device, const std::string& metallibPath, MTLPixelFormat outputFormat, std::string& err);

    // Upload the per-spin tables whenever SceneCache::version changes.
    void syncTables(const SceneCache& c);
    void generateSky(int faceSize);

    // Size of the ray-traced image (may be smaller than the output; the composite pass upsamples).
    void ensureTraceSize(int w, int h);
    int traceWidth() const { return traceW_; }
    int traceHeight() const { return traceH_; }

    // trace (optional) → accumulate → bloom → composite into `target`
    void encodeFrame(id<MTLCommandBuffer> cb, const FrameUniforms& U, bool doTrace, float accumWeight,
                     const PostUniforms& P, id<MTLTexture> target);

    // Debug pass: raw trace results (status, dir / first hit) in an RGBA32F texture of the given size.
    std::vector<simd_float4> traceDebug(const FrameUniforms& U, int w, int h);
    bool cubeConventionTest(std::string& report);

    id<MTLDevice> device() const { return dev_; }
    id<MTLCommandQueue> queue() const { return queue_; }

private:
    id<MTLComputePipelineState> compute(NSString* name, std::string& err);

    id<MTLDevice> dev_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    id<MTLLibrary> lib_ = nil;
    id<MTLRenderPipelineState> tracePSO_ = nil, compositePSO_ = nil;
    id<MTLComputePipelineState> accumPSO_ = nil, downPSO_ = nil, upPSO_ = nil, skyPSO_ = nil;
    id<MTLComputePipelineState> cubeWritePSO_ = nil, cubeReadPSO_ = nil;
    id<MTLTexture> sky_ = nil, traceTex_ = nil, accumTex_ = nil, dummy_ = nil;
    std::vector<id<MTLTexture>> down_, up_;
    id<MTLBuffer> flux_ = nil, lut_ = nil, funnel_ = nil;
    int tableVersion_ = -1;
    int traceW_ = 0, traceH_ = 0;
    MTLPixelFormat outFormat_ = MTLPixelFormatBGRA8Unorm;
};

// PNG writer for BGRA8 pixels.
bool writePNG(const std::string& path, const uint8_t* bgra, int w, int h, int bytesPerRow);
