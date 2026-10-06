// shared_types.h -- structures shared by the C++/Objective-C++ host and the Metal shaders.
// Only float4-aligned members are used so both sides agree on the layout.
#pragma once

#ifdef __METAL_VERSION__
// metal_stdlib is already included by kerr_shared.h
#else
#include <simd/simd.h>
typedef simd::float4 float4;
typedef simd::float2 float2;
typedef simd::float3 float3;
typedef simd::float4x4 float4x4;
#endif

// ---- ray tracer ------------------------------------------------------------------------------------
struct RTUniforms {
    float4 cam;   // r0, theta0, phi0, tan(fov_y/2)
    float4 bh;    // a, r_plus, r_isco, r_out
    float4 view;  // width, height (internal), sim time [M], frame index
    float4 jit;   // subpixel jitter x,y ; disk texture-time offset ; unused
    float4 disk;  // T_peak [K], disk on, corona on, stars on
    float4 opt;   // lensed celestial grid on, debug view, step coefficient c, max steps
    float4 sky;   // sky brightness, star size [rad], Milky Way strength, r_far
    float4 misc;  // turbulence amount, disk brightness, halo strength, unused
};

// ---- post processing -------------------------------------------------------------------------------
struct PostUniforms {
    float4 a;      // history blend for static content, for moving content, exposure, bloom strength
    float4 b;      // internal width, internal height, drawable width, drawable height
    float4 c;      // funnel inset x0, y0, x1, y1 (pixels; x1 <= x0 => no inset)
    float4 d;      // funnel fullscreen (1/0), hud on, vignette, saturation
    float4 e;      // time, frame index, upscale mode (0 bilinear, 1 catmull-rom), unused
};

// ---- funnel (spacetime embedding) view ---------------------------------------------------------------
struct FunnelUniforms {
    float4x4 viewProj;
    float4 camPos;    // world-space camera position, aspect
    float4 bh;        // a, r_plus, r_isco, r_out
    float4 radii;     // r_photon, r_ergo(equator), r_camera, phi_camera
    float4 time;      // sim time, T_peak, viewport width, viewport height
    float4 opt;       // lattice spacing, z exaggeration, depth scale, disk on
    float4 camRight;  // camera right vector (world) for billboards
    float4 camUp;
};

struct FunnelVertex {
    float4 pos;   // xyz world position, w unused
    float4 attr;  // x, y lattice coordinates (r cos phi, r sin phi); z = r ; w = tube depth (>=0) or -1 for the sheet
};

struct ParticleVertex {
    float4 pos;    // xyz world, w = size (world units)
    float4 color;  // premultiplied-ish additive colour, a = intensity
};
