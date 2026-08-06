#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

// MPS (Metal Performance Shaders) interoperability stub
// Provides GPU-accelerated graph computation on Apple Silicon

bool MPSAvailable() {
    return MTLCreateSystemDefaultDevice() != nil;
}
