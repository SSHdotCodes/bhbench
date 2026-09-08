// renderer.h — Metal renderer for the Kerr black hole simulation (Objective-C++ interface).
#pragma once
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

struct BHOptions {
    double spin = 0.9;
    double inclinationDeg = 76.0;
    double camR = 42.0;
    double renderScale = 0.5;      // ray-traced resolution relative to the drawable
    double tScale = 40000.0;   // T₀ in kelvin: T(r) = T₀ · (8πF/3)^{1/4}
    int frames = 0;                 // > 0: render this many frames, save screenshot, exit
    NSString *screenshotPath = nil;
    NSString *dumpPath = nil;
    NSString *shaderDir = nil;
    int gridMode = 1;
    bool bloom = true;
    bool disk = true;
};

@interface BHRenderer : NSObject <MTKViewDelegate>
- (instancetype)initWithView:(MTKView *)view hud:(NSTextField *)hud options:(const BHOptions &)opts;
- (void)keyDown:(NSEvent *)event;
- (void)mouseDrag:(CGFloat)dx dy:(CGFloat)dy secondary:(BOOL)secondary;
- (void)scroll:(CGFloat)dy;
- (void)requestScreenshot:(NSString *)path;
@end
