#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include <chrono>
#include <iostream>
#include "ShaderTypes.h"
#include "Camera.h"
#include "../external/imgui/imgui.h"
#include "../external/imgui/backends/imgui_impl_metal.h"
#include "../external/imgui/backends/imgui_impl_osx.h"

@interface BlackHoleRenderer : NSObject <MTKViewDelegate>
- (instancetype)initWithMetalKitView:(MTKView *)view;
- (void)handleMouseDragWithDeltaX:(float)dx deltaY:(float)dy isRightButton:(BOOL)right;
- (void)handleScrollWithDeltaY:(float)dy;
@end

@implementation BlackHoleRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLComputePipelineState> _rayTracePipeline;
    id<MTLTexture> _renderTexture;
    
    SimulationUniforms _uniforms;
    Camera _camera;
    
    std::chrono::high_resolution_clock::time_point _startTime;
    std::chrono::high_resolution_clock::time_point _lastFrameTime;
    float _fps;
    float _frameTimeMs;
    
    // Interactive UI controls
    bool _showUI;
    bool _autoOrbit;
    float _orbitSpeed;
}

- (instancetype)initWithMetalKitView:(MTKView *)view {
    self = [super init];
    if (self) {
        _device = view.device;
        _commandQueue = [_device newCommandQueue];
        _startTime = std::chrono::high_resolution_clock::now();
        _lastFrameTime = _startTime;
        _showUI = true;
        _autoOrbit = true;
        _orbitSpeed = 0.08f;
        _fps = 60.0f;
        _frameTimeMs = 16.6f;
        
        [self initUniforms];
        [self buildPipelines];
        
        // Setup ImGui for Metal + macOS
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        
        // Custom sleek astrophysical sci-fi theme for ImGui
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 5.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 0.88f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.14f, 0.20f, 0.95f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.22f, 0.35f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.38f, 0.80f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.35f, 0.52f, 0.90f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.28f, 0.42f, 0.85f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.40f, 0.60f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.50f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.38f, 0.65f, 0.98f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.24f, 0.80f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.35f, 0.90f);
        
        ImGui_ImplMetal_Init(_device);
        ImGui_ImplOSX_Init(view);
    }
    return self;
}

- (void)initUniforms {
    memset(&_uniforms, 0, sizeof(_uniforms));
    
    _uniforms.mass = 1.0f;
    _uniforms.spin = 0.88f; // Kerr spin parameter a/M
    
    // Kerr outer event horizon: r_+ = M + sqrt(M^2 - a^2)
    float a = _uniforms.spin;
    _uniforms.horizonRadius = _uniforms.mass + std::sqrt(std::max(0.001f, _uniforms.mass * _uniforms.mass - a * a));
    
    // Kerr ISCO (Innermost Stable Circular Orbit) calculation
    float z1 = 1.0f + std::cbrt(1.0f - a * a) * (std::cbrt(1.0f + a) + std::cbrt(1.0f - a));
    float z2 = std::sqrt(3.0f * a * a + z1 * z1);
    float r_isco = _uniforms.mass * (3.0f + z2 - std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2)));
    _uniforms.iscoRadius = r_isco;
    
    // Accretion disk settings
    _uniforms.diskInnerRadius = r_isco * 0.98f; // Accretion disk extends from ISCO outward
    _uniforms.diskOuterRadius = 14.5f;
    _uniforms.diskScaleHeight = 0.045f;
    _uniforms.diskTemperatureBase = 1.0f;
    _uniforms.diskDensity = 0.85f;
    _uniforms.diskSpeedMultiplier = 1.0f;
    _uniforms.dopplerStrength = 3.5f;
    _uniforms.diskTextureMode = 0;
    
    // Corona / Spherical Halo settings
    _uniforms.haloIntensity = 0.7f;
    _uniforms.haloRadius = 4.5f;
    _uniforms.haloFalloff = 2.4f;
    _uniforms.enableCorona = 1;
    
    // Spacetime Curvature & Flamm Trapdoor Grid settings
    _uniforms.enableSpacetimeGrid = 1;
    _uniforms.gridSpacing = 1.0f;
    _uniforms.gridThickness = 0.08f;
    _uniforms.gridDepthScale = 0.85f;
    _uniforms.gridAlpha = 0.65f;
    _uniforms.gridType = 0;
    
    // Ray Tracer Integrator Settings
    _uniforms.maxSteps = 320;
    _uniforms.stepSize = 0.08f;
    _uniforms.adaptiveStepFactor = 1.0f;
    _uniforms.escapeRadius = 50.0f;
    
    // Post Processing & Celestial Sky
    _uniforms.renderMode = 0;
    _uniforms.exposure = 1.35f;
    _uniforms.bloomThreshold = 0.8f;
    _uniforms.bloomIntensity = 0.6f;
    _uniforms.enableSkybox = 1;
    _uniforms.celestialLensingMode = 0;
    _uniforms.qualityLevel = 1;
    _uniforms.showPhotonRingGlow = 1;
}

- (void)buildPipelines {
    NSError *error = nil;
    
    // Try loading precompiled metallib first, otherwise compile from source
    NSString *metallibPath = [[NSBundle mainBundle] pathForResource:@"BlackHoleRaymarch" ofType:@"metallib"];
    if (!metallibPath) {
        metallibPath = @"shaders/BlackHoleRaymarch.metallib";
    }
    
    id<MTLLibrary> library = nil;
    if ([[NSFileManager defaultManager] fileExistsAtPath:metallibPath]) {
        library = [_device newLibraryWithURL:[NSURL fileURLWithPath:metallibPath] error:&error];
    }
    
    if (!library) {
        NSString *shaderSourcePath = @"shaders/BlackHoleRaymarch.metal";
        NSString *source = [NSString stringWithContentsOfFile:shaderSourcePath encoding:NSUTF8StringEncoding error:&error];
        if (source) {
            MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
            library = [_device newLibraryWithSource:source options:options error:&error];
        }
    }
    
    if (!library) {
        NSLog(@"Error creating Metal library: %@", error);
        return;
    }
    
    id<MTLFunction> kernelFunc = [library newFunctionWithName:@"blackHoleRayTraceKernel"];
    if (!kernelFunc) {
        NSLog(@"Error: blackHoleRayTraceKernel function not found in library");
        return;
    }
    
    _rayTracePipeline = [_device newComputePipelineStateWithFunction:kernelFunc error:&error];
    if (!_rayTracePipeline) {
        NSLog(@"Error creating compute pipeline: %@", error);
    }
}

- (void)handleMouseDragWithDeltaX:(float)dx deltaY:(float)dy isRightButton:(BOOL)right {
    if (right) {
        _camera.pan(dx * 0.005f, dy * 0.005f);
    } else {
        _camera.orbit(dx * 0.006f, -dy * 0.006f);
    }
}

- (void)handleScrollWithDeltaY:(float)dy {
    _camera.zoom(-dy * 0.5f);
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    MTLTextureDescriptor *textureDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                           width:size.width
                                                                                          height:size.height
                                                                                       mipmapped:NO];
    textureDesc.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    _renderTexture = [_device newTextureWithDescriptor:textureDesc];
}

- (void)renderImGui:(MTKView *)view commandBuffer:(id<MTLCommandBuffer>)commandBuffer {
    ImGui_ImplMetal_NewFrame(view.currentRenderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(view.bounds.size.width, view.bounds.size.height);
    
    ImGui::NewFrame();
    
    if (_showUI) {
        ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(410, 680), ImGuiCond_FirstUseEver);
        
        ImGui::Begin("Relativistic Kerr Black Hole Simulation", &_showUI);
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "General Relativistic Ray-Tracer");
        ImGui::Text("GPU: %s", [_device.name UTF8String]);
        ImGui::Text("FPS: %.1f (%.2f ms)", _fps, _frameTimeMs);
        ImGui::Separator();
        
        if (ImGui::CollapsingHeader("Black Hole Physics (Kerr Metric)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Spin a/M", &_uniforms.spin, -0.998f, 0.998f, "%.3f")) {
                float a = _uniforms.spin;
                _uniforms.horizonRadius = _uniforms.mass + std::sqrt(std::max(0.001f, _uniforms.mass * _uniforms.mass - a * a));
                
                float z1 = 1.0f + std::cbrt(1.0f - a * a) * (std::cbrt(1.0f + a) + std::cbrt(1.0f - a));
                float z2 = std::sqrt(3.0f * a * a + z1 * z1);
                float sign = (a >= 0.0f) ? 1.0f : -1.0f;
                float r_isco = _uniforms.mass * (3.0f + z2 - sign * std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2)));
                _uniforms.iscoRadius = r_isco;
                _uniforms.diskInnerRadius = r_isco;
            }
            ImGui::Text("Event Horizon (r+): %.3f M", _uniforms.horizonRadius);
            ImGui::Text("Photon Sphere (r_ph): %.3f M", 3.0f * _uniforms.mass);
            ImGui::Text("ISCO Radius: %.3f M", _uniforms.iscoRadius);
        }
        
        if (ImGui::CollapsingHeader("Accretion Disk & Plasma", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Inner Radius", &_uniforms.diskInnerRadius, _uniforms.horizonRadius, 10.0f, "%.2f M");
            ImGui::SliderFloat("Outer Radius", &_uniforms.diskOuterRadius, 5.0f, 30.0f, "%.2f M");
            ImGui::SliderFloat("Scale Height (H/R)", &_uniforms.diskScaleHeight, 0.01f, 0.20f, "%.3f");
            ImGui::SliderFloat("Plasma Density", &_uniforms.diskDensity, 0.1f, 3.0f, "%.2f");
            ImGui::SliderFloat("Doppler Beaming", &_uniforms.dopplerStrength, 0.0f, 5.0f, "%.1f");
            ImGui::SliderFloat("Disk Orbit Speed", &_uniforms.diskSpeedMultiplier, 0.0f, 3.0f, "%.2f");
        }
        
        if (ImGui::CollapsingHeader("Compton Corona / Halo Glow", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool enableCor = _uniforms.enableCorona == 1;
            if (ImGui::Checkbox("Enable Corona", &enableCor)) {
                _uniforms.enableCorona = enableCor ? 1 : 0;
            }
            ImGui::SliderFloat("Halo Intensity", &_uniforms.haloIntensity, 0.0f, 2.5f, "%.2f");
            ImGui::SliderFloat("Halo Radius", &_uniforms.haloRadius, 2.0f, 10.0f, "%.2f M");
            ImGui::SliderFloat("Halo Falloff", &_uniforms.haloFalloff, 1.0f, 5.0f, "%.1f");
        }
        
        if (ImGui::CollapsingHeader("Spacetime Curvature & Trapdoor Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool enableGrid = _uniforms.enableSpacetimeGrid == 1;
            if (ImGui::Checkbox("Show 3D Trapdoor Grid (Flamm Paraboloid)", &enableGrid)) {
                _uniforms.enableSpacetimeGrid = enableGrid ? 1 : 0;
            }
            ImGui::SliderFloat("Grid Spacing", &_uniforms.gridSpacing, 0.2f, 3.0f, "%.2f M");
            ImGui::SliderFloat("Trapdoor Depth Scale", &_uniforms.gridDepthScale, 0.1f, 2.0f, "%.2f");
            ImGui::SliderFloat("Grid Line Thickness", &_uniforms.gridThickness, 0.01f, 0.25f, "%.2f");
            ImGui::SliderFloat("Grid Opacity", &_uniforms.gridAlpha, 0.1f, 1.0f, "%.2f");
        }
        
        if (ImGui::CollapsingHeader("Scientific Visualization & Render Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* modes[] = { "Relativistic Photorealistic", "Geodesic Deflection Heatmap", "Doppler Redshift / Blueshift Map" };
            ImGui::Combo("View Mode", &_uniforms.renderMode, modes, IM_ARRAYSIZE(modes));
            
            const char* skyModes[] = { "Milky Way Galaxy & Stars", "Celestial Coordinate Lensing Grid" };
            ImGui::Combo("Celestial Background", &_uniforms.celestialLensingMode, skyModes, IM_ARRAYSIZE(skyModes));
            
            bool photonGlow = _uniforms.showPhotonRingGlow == 1;
            if (ImGui::Checkbox("Emphasize Photon Ring Sub-structures", &photonGlow)) {
                _uniforms.showPhotonRingGlow = photonGlow ? 1 : 0;
            }
            
            ImGui::SliderFloat("HDR Exposure", &_uniforms.exposure, 0.2f, 3.5f, "%.2f");
            ImGui::SliderInt("Max RK4 Steps", &_uniforms.maxSteps, 50, 600);
            ImGui::SliderFloat("Integration Step Size", &_uniforms.stepSize, 0.02f, 0.20f, "%.3f");
        }
        
        if (ImGui::CollapsingHeader("Camera & Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto-Orbit Camera", &_autoOrbit);
            if (_autoOrbit) {
                ImGui::SliderFloat("Orbit Speed", &_orbitSpeed, -0.5f, 0.5f, "%.3f rad/s");
            }
            ImGui::SliderFloat("Camera Distance", &_camera.distance, 4.0f, 60.0f, "%.1f M");
            ImGui::SliderFloat("Azimuth", &_camera.azimuth, 0.0f, 6.28f, "%.2f");
            ImGui::SliderFloat("Elevation", &_camera.elevation, -1.4f, 1.4f, "%.2f");
            
            if (ImGui::Button("Reset Camera View")) {
                _camera = Camera();
            }
        }
        
        ImGui::End();
    }
    
    ImGui::Render();
    
    MTLRenderPassDescriptor *renderPassDesc = view.currentRenderPassDescriptor;
    if (renderPassDesc) {
        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDesc];
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
        [renderEncoder endEncoding];
    }
}

- (void)drawInMTKView:(MTKView *)view {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(currentTime - _lastFrameTime).count();
    _lastFrameTime = currentTime;
    
    if (dt > 0.0f) {
        float currentFPS = 1.0f / dt;
        _fps = _fps * 0.9f + currentFPS * 0.1f;
        _frameTimeMs = dt * 1000.0f;
    }
    
    float totalTime = std::chrono::duration<float>(currentTime - _startTime).count();
    _uniforms.time = totalTime;
    
    if (_autoOrbit) {
        _camera.orbit(_orbitSpeed * dt, 0.0f);
    }
    _camera.updatePosition();
    
    // Update Camera Uniforms
    _uniforms.camPos = _camera.position;
    _uniforms.camTarget = _camera.target;
    _uniforms.camUp = _camera.up;
    _uniforms.fovY = _camera.fovY;
    
    CGSize drawableSize = view.drawableSize;
    if (drawableSize.width <= 0 || drawableSize.height <= 0) return;
    
    _uniforms.screenResolution = simd::make_uint2((uint32_t)drawableSize.width, (uint32_t)drawableSize.height);
    _uniforms.aspectRatio = (float)drawableSize.width / (float)drawableSize.height;
    
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!drawable) return;
    
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    
    // 1. Ray Tracing Compute Pass
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:_rayTracePipeline];
    [computeEncoder setTexture:drawable.texture atIndex:0];
    [computeEncoder setBytes:&_uniforms length:sizeof(SimulationUniforms) atIndex:0];
    
    MTLSize threadgroupSize = MTLSizeMake(16, 16, 1);
    MTLSize threadgroups = MTLSizeMake((drawableSize.width + threadgroupSize.width - 1) / threadgroupSize.width,
                                       (drawableSize.height + threadgroupSize.height - 1) / threadgroupSize.height,
                                       1);
    
    [computeEncoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadgroupSize];
    [computeEncoder endEncoding];
    
    // 2. Render ImGui UI on top
    [self renderImGui:view commandBuffer:commandBuffer];
    
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end
