#include "MetalRenderer.h"
#include <iostream>

MetalRenderer::MetalRenderer()
    : device(nil), commandQueue(nil), metalLayer(nil),
      computePipeline(nil), renderPipeline(nil),
      raytraceTexture(nil), hudTexture(nil), paramsBuffer(nil),
      currentWidth(0), currentHeight(0) {}

MetalRenderer::~MetalRenderer() {}

bool MetalRenderer::init(CAMetalLayer* layer, int width, int height) {
    metalLayer = layer;
    device = metalLayer.device;
    if (!device) {
        device = MTLCreateSystemDefaultDevice();
        metalLayer.device = device;
    }
    
    commandQueue = [device newCommandQueue];
    if (!commandQueue) {
        std::cerr << "Failed to create Metal command queue\n";
        return false;
    }

    if (!buildPipelines()) {
        std::cerr << "Failed to build Metal pipelines\n";
        return false;
    }

    paramsBuffer = [device newBufferWithLength:sizeof(BlackHoleParams)
                                       options:MTLResourceStorageModeShared];

    resize(width, height);
    return true;
}

bool MetalRenderer::buildPipelines() {
    NSError* error = nil;
    
    // Load header and shader source code
    NSString* headerPath = @"ShaderTypes.h";
    NSString* headerSource = [NSString stringWithContentsOfFile:headerPath
                                                       encoding:NSUTF8StringEncoding
                                                          error:nil];
    if (!headerSource) {
        headerPath = @"./ShaderTypes.h";
        headerSource = [NSString stringWithContentsOfFile:headerPath
                                                 encoding:NSUTF8StringEncoding
                                                    error:nil];
    }

    NSString* shaderPath = @"BlackHoleShader.metal";
    NSString* source = [NSString stringWithContentsOfFile:shaderPath
                                                encoding:NSUTF8StringEncoding
                                                   error:&error];
    if (!source) {
        // Try relative to bundle or directory
        shaderPath = @"./BlackHoleShader.metal";
        source = [NSString stringWithContentsOfFile:shaderPath
                                          encoding:NSUTF8StringEncoding
                                             error:&error];
    }
    
    if (!source) {
        std::cerr << "Failed to load BlackHoleShader.metal: " 
                  << (error ? [[error localizedDescription] UTF8String] : "Unknown error") << std::endl;
        return false;
    }

    // Prepend header if available so #include isn't needed
    NSString* combinedSource = source;
    if (headerSource) {
        // Remove the #include line and prepend header
        NSString* strippedShader = [source stringByReplacingOccurrencesOfString:@"#include \"ShaderTypes.h\"" withString:@""];
        combinedSource = [NSString stringWithFormat:@"%@\n%@", headerSource, strippedShader];
    }

    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
#if defined(__MAC_15_0)
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeFast;
    } else {
        options.fastMathEnabled = YES;
    }
#else
    options.fastMathEnabled = YES;
#endif
    
    id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
    if (!library) {
        std::cerr << "Metal compilation error: " 
                  << (error ? [[error localizedDescription] UTF8String] : "Unknown") << std::endl;
        return false;
    }

    id<MTLFunction> kernelFunc = [library newFunctionWithName:@"raytraceBlackHole"];
    if (!kernelFunc) {
        std::cerr << "Function 'raytraceBlackHole' not found in shader\n";
        return false;
    }

    computePipeline = [device newComputePipelineStateWithFunction:kernelFunc error:&error];
    if (!computePipeline) {
        std::cerr << "Compute pipeline creation error: "
                  << (error ? [[error localizedDescription] UTF8String] : "Unknown") << std::endl;
        return false;
    }

    id<MTLFunction> vertFunc = [library newFunctionWithName:@"blitVertex"];
    id<MTLFunction> fragFunc = [library newFunctionWithName:@"blitFragment"];

    MTLRenderPipelineDescriptor* renderDesc = [[MTLRenderPipelineDescriptor alloc] init];
    renderDesc.vertexFunction = vertFunc;
    renderDesc.fragmentFunction = fragFunc;
    renderDesc.colorAttachments[0].pixelFormat = metalLayer.pixelFormat;
    
    renderPipeline = [device newRenderPipelineStateWithDescriptor:renderDesc error:&error];
    if (!renderPipeline) {
        std::cerr << "Render pipeline creation error: "
                  << (error ? [[error localizedDescription] UTF8String] : "Unknown") << std::endl;
        return false;
    }

    return true;
}

void MetalRenderer::createTextures(int width, int height) {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor 
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    raytraceTexture = [device newTextureWithDescriptor:desc];

    MTLTextureDescriptor* hudDesc = [MTLTextureDescriptor 
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    hudDesc.usage = MTLTextureUsageShaderRead;
    hudTexture = [device newTextureWithDescriptor:hudDesc];

    hud.resize(width, height);
}

void MetalRenderer::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == currentWidth && height == currentHeight) return;

    currentWidth = width;
    currentHeight = height;
    metalLayer.drawableSize = CGSizeMake(width, height);

    createTextures(width, height);
}

void MetalRenderer::updateHUD(const std::string& statsText, bool showHUD) {
    hud.clear();
    if (!showHUD) {
        // Upload empty hud texture
        MTLRegion region = MTLRegionMake2D(0, 0, currentWidth, currentHeight);
        [hudTexture replaceRegion:region
                      mipmapLevel:0
                        withBytes:hud.getPixelBuffer()
                      bytesPerRow:currentWidth * sizeof(uint32_t)];
        return;
    }

    // Semi-transparent HUD background panel
    int panelW = 420;
    int panelH = 340;
    int panelX = 20;
    int panelY = 20;
    // Dark slate background with border
    hud.fillRect(panelX, panelY, panelW, panelH, 0xD010151E);
    hud.drawRect(panelX, panelY, panelW, panelH, 0xFF3565A0);
    hud.drawRect(panelX + 1, panelY + 1, panelW - 2, panelH - 2, 0x80204060);

    // Render title
    hud.drawText(panelX + 16, panelY + 14, "GENERAL RELATIVITY BLACK HOLE", 0xFF00E5FF, 1);
    hud.drawText(panelX + 16, panelY + 28, "Real-time Kerr Spacetime Geodesics", 0xFF88A0B0, 1);

    // Divider line
    hud.fillRect(panelX + 16, panelY + 44, panelW - 32, 1, 0xFF3565A0);

    // Stats & controls text
    hud.drawText(panelX + 16, panelY + 54, statsText, 0xFFE0E0E0, 1);

    // Upload to Metal texture
    MTLRegion region = MTLRegionMake2D(0, 0, currentWidth, currentHeight);
    [hudTexture replaceRegion:region
                  mipmapLevel:0
                    withBytes:hud.getPixelBuffer()
                  bytesPerRow:currentWidth * sizeof(uint32_t)];
}

void MetalRenderer::render(const BlackHoleParams& params) {
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
        if (!drawable) return;

        // Copy params into Metal buffer
        memcpy([paramsBuffer contents], &params, sizeof(BlackHoleParams));

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

        // 1. Compute Pass: Raytrace null geodesics
        id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
        [computeEncoder setComputePipelineState:computePipeline];
        [computeEncoder setTexture:raytraceTexture atIndex:0];
        [computeEncoder setBuffer:paramsBuffer offset:0 atIndex:0];

        MTLSize threadgroupSize = MTLSizeMake(16, 16, 1);
        MTLSize gridSize = MTLSizeMake(
            (currentWidth + threadgroupSize.width - 1) / threadgroupSize.width,
            (currentHeight + threadgroupSize.height - 1) / threadgroupSize.height,
            1);

        [computeEncoder dispatchThreadgroups:gridSize threadsPerThreadgroup:threadgroupSize];
        [computeEncoder endEncoding];

        // 2. Render Pass: Fullscreen quad blit + composite HUD
        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        [renderEncoder setRenderPipelineState:renderPipeline];
        [renderEncoder setFragmentTexture:raytraceTexture atIndex:0];
        [renderEncoder setFragmentTexture:hudTexture atIndex:1];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [renderEncoder endEncoding];

        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
}
