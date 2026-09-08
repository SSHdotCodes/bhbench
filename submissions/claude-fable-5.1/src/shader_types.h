// shader_types.h — uniform layouts shared by the C++ host and the Metal shaders.
#ifndef BH_SHADER_TYPES_H
#define BH_SHADER_TYPES_H
#include <simd/simd.h>

#define BH_MAX_LAYERS 8

typedef struct {
    float a, rh, rIsco, rDiskOut;
    float rGridOut, rEsc, rStop, rDoom;
    float camR, camTh, camPh, tanHalfFov;
    float aspect, jitterX, jitterY, eps;
    float epsAng, camX, camY, camZ;
    float yaw, pitch, pad0, pad1;
    unsigned int width, height, layer, maxSteps;
    unsigned int diskOn, gridOn, pad2, pad3;
} TraceUniforms;

typedef struct {
    float a, rIsco, rDiskOut, rGridOut;
    float tMax, time, pixelAngle, turbulence;
    float logTmin, logTmax, gridSpacing, skyBrightness;
    float rh, limbDarkening, pad0, pad1;
    unsigned int width, height, layers, lutN;
    unsigned int diskLutN, flags, pad2, pad3;
} ShadeUniforms;

typedef struct {
    simd_float4x4 viewProj;
    simd_float3 camPos; float depthFar;
    float rh, gridSpacing, alpha, zMin;
    float rOut, time, pad0, pad1;
} GridUniforms;

typedef struct {
    float exposure, bloomStrength, depthFar, pad0;
    unsigned int width, height, flags, pad1;
} BlitUniforms;

typedef struct {
    float threshold, knee, pad0, pad1;
    unsigned int srcW, srcH, dstW, dstH;
    int dirX, dirY, pad2, pad3;
} BloomUniforms;

#endif
