// Renderer.h — Metal renderer: compute ray tracing + grid overlay.
#pragma once
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <string>

// Immutable per-frame snapshot of the interactive state.
struct SimSnapshot {
    float yaw = 0.0f, pitch = 0.30f, dist = 18.0f;
    bool grid = true, disk = true, sky = true, beaming = true, redshift = true;
    bool bgRaytrace = true;          // false -> plain black background
    int  maxSteps = 1500;
    float renderScale = 0.75f;
    float exposure = 1.0f;
    bool shot = false;               // capture a screenshot this frame
    std::string shotPath;
};

@interface Renderer : NSObject
- (instancetype)initWithLayer:(CAMetalLayer*)layer error:(NSError**)err;
- (void)drawSnapshot:(const SimSnapshot&)snap time:(float)t drawableSize:(CGSize)size;
@end
