#pragma once

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
typedef float3 bh_float3;
typedef float4 bh_float4;
typedef float4x4 bh_float4x4;
#else
#include <simd/simd.h>
typedef simd_float3 bh_float3;
typedef simd_float4 bh_float4;
typedef simd_float4x4 bh_float4x4;
#endif

struct SimulationUniforms {
    // Camera world position (X, Y, Z) in Z-up physics coordinates + time
    bh_float4 camPosTime;       // xyz = camPos, w = simulation time
    bh_float4 camForwardFov;    // xyz = forward, w = tan(fovY / 2)
    bh_float4 camRightAspect;   // xyz = right,   w = aspect (width / height)
    bh_float4 camUpDummy;       // xyz = up,      w = unused

    // Camera Boyer-Lindquist coordinates (r, theta, phi, sinTheta)
    bh_float4 camBL;            // x = r0, y = theta0, z = phi0, w = cosTheta0

    // Black hole & accretion disk physics
    bh_float4 bhParams;         // x = spin a [0..0.998], y = r_+ (horizon), z = r_ISCO, w = diskOuterR
    bh_float4 diskParams;       // x = diskEnabled (0/1), y = dopplerEnabled (0/1), z = diskBrightness, w = haloIntensity
    bh_float4 gridParams;       // x = gridEnabled (0/1), y = gridVerticalOffset, z = gridOuterR, w = gridOpacity
    bh_float4 visualParams;     // x = backgroundMode (0=Space, 1=EinsteinGrid), y = bloomIntensity, z = infallParticlesEnabled, w = exposure
    bh_float4 screenParams;     // x = width, y = height, z = 1/width, w = 1/height

    bh_float4x4 viewProj;       // View-Projection matrix for 3D Spacetime Curvature Grid
};

struct GridVertex {
    bh_float4 posAndInfo;       // xyz = 3D world position on Kerr/Flamm embedding, w = radial parameter r
    bh_float4 normalAndTwist;   // xyz = surface normal, w = frame-dragging twist angle deltaPhi
};
