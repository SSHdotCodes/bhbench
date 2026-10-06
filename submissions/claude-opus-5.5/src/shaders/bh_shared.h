// bh_shared.h — data layouts shared between the C++ host code and the Metal shaders.
//
// Both sides use 16-byte aligned 3-vectors (simd_float3 / float3), so the struct layouts are identical.
#ifndef BH_SHARED_H
#define BH_SHARED_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
typedef metal::float2 bh_float2;
typedef metal::float3 bh_float3;
typedef metal::float4 bh_float4;
#else
#include <simd/simd.h>
typedef simd_float2 bh_float2;
typedef simd_float3 bh_float3;
typedef simd_float4 bh_float4;
#endif

// Feature flags (FrameUniforms::flags)
enum {
    BH_FLAG_DISK        = 1 << 0,   // accretion disk present
    BH_FLAG_GR          = 1 << 1,   // curved (Kerr) ray paths; off = straight lines in flat space
    BH_FLAG_REDSHIFT    = 1 << 2,   // relativistic Doppler + gravitational shift of disk and sky light
    BH_FLAG_TURBULENCE  = 1 << 3,   // co-rotating MRI-like brightness fluctuations in the disk
    BH_FLAG_LIMB        = 1 << 4,   // electron-scattering limb darkening (Chandrasekhar)
    BH_FLAG_ORDER       = 1 << 5,   // colour-code disk images by order (number of equator crossings)
    BH_FLAG_SHADOW      = 1 << 6,   // overlay the analytic (Bardeen) shadow boundary
    BH_FLAG_DEBUG       = 1 << 7,   // write raw trace results instead of colour (self-test)
    BH_FLAG_SKY         = 1 << 8,   // background star field
};

// Grid modes (FrameUniforms::gridMode)
enum { BH_GRID_OFF = 0, BH_GRID_FUNNEL = 1, BH_GRID_PLANE = 2 };

// Trace status codes (also written by the debug output)
enum { BH_TRACE_ESCAPED = 0, BH_TRACE_CAPTURED = 1, BH_TRACE_ABSORBED = 2, BH_TRACE_MAXSTEPS = 3 };

#define BH_NUM_SPECIAL 5   // horizon, ergosphere (equator), prograde photon orbit, retrograde photon orbit, ISCO

struct FrameUniforms {
    // camera axes and 3-velocity, as components on the local ZAMO triad (e_r, e_theta, e_phi)
    bh_float3 camRight;
    bh_float3 camUp;
    bh_float3 camFwd;
    bh_float3 camBeta;

    bh_float2 resolution;      // trace target size in pixels
    bh_float2 jitter;          // sub-pixel offset in pixels (progressive supersampling)

    float tanHalfFov, aspect, pixelAngle, simTime;          // simTime: coordinate time at the camera [M]
    float camR, camSinT, camCosT, camSinP;
    float camCosP, camGamma, camSigma, camDelta;
    float camA, camAlpha, camOmega, camVarpi;

    float spin, mass, rPlus, rIsco;                          // geometry used for the rays (mass 0 = flat)
    float rhoHorizon, rhoCapture, diskSpin, rDiskOut;        // diskSpin: true spin (for the disk orbits)
    float diskRin, diskTpeak, diskOpacity, turbAmp;
    float exposure, diskExposure, skyGain, gridGain;
    float stepAng, stepRho, stepPhi, gridRmax;
    float funnelR0, funnelR1, funnelRho, funnelSurface;
    float lutLogT0, lutLogT1, bloomDummy, pad0;

    float specialPlane[BH_NUM_SPECIAL];                      // BL radii of the special circles
    float specialFunnel[BH_NUM_SPECIAL];                     // the same circles on the embedding surface

    int flags, gridMode, maxSteps, fluxCount;
    int lutCount, funnelCount, pad1, pad2;
};

struct PostUniforms {
    bh_float2 outSize;          // drawable size in pixels
    bh_float2 srcScale;         // unused (kept for alignment)
    float bloomStrength, bloomNorm, stretch, frameIndex;
    int tonemap, bloomOn, pad0, pad1;
};

struct SkyParams {
    bh_float3 galX, galY, galZ;   // rows of the rotation BL-Cartesian -> galactic frame
    float starGrid, starProb, starFlux, milkyWay;
    int size, pad0, pad1, pad2;
};

#endif
