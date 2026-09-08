#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "ShaderTypes.h"
#include "HUDOverlay.h"
#include <string>

class MetalRenderer {
public:
    MetalRenderer();
    ~MetalRenderer();

    bool init(CAMetalLayer* layer, int width, int height);
    void resize(int width, int height);
    void updateHUD(const std::string& statsText, bool showHUD);
    void render(const BlackHoleParams& params);

    id<MTLDevice> getDevice() const { return device; }

private:
    bool buildPipelines();
    void createTextures(int width, int height);

    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
    CAMetalLayer* metalLayer;

    id<MTLComputePipelineState> computePipeline;
    id<MTLRenderPipelineState> renderPipeline;

    id<MTLTexture> raytraceTexture;
    id<MTLTexture> hudTexture;
    id<MTLBuffer> paramsBuffer;

    HUDOverlay hud;
    int currentWidth;
    int currentHeight;
};
