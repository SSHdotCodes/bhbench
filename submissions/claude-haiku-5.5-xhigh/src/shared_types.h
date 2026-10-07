// Plain structs shared byte-for-byte by the C++ host and the Metal shaders.
// Only 4-byte scalars and arrays of them are used, so the layout is identical on both sides.
#ifndef BH_SHARED_TYPES_H
#define BH_SHARED_TYPES_H

#ifndef __METAL_VERSION__
typedef unsigned int uint;
#endif

// Everything the ray tracer needs for one frame.
struct TraceUniforms {
    float a;             // dimensionless spin, 0 <= a < 1 (units of M)
    float r_plus;        // outer horizon 1 + sqrt(1 - a^2)
    float r_capture;     // photons inside this radius are captured (see capture_radius)
    float r_isco;        // prograde innermost stable circular orbit
    float r_out;         // outer edge of the accretion disk
    float eta;           // RK4 step fraction: each step moves a photon about eta * |x| (see step_length)
    float r_escape;      // |x| beyond which an outgoing photon is counted as escaped
    float cam_ut;        // u^t of the static camera = 1/sqrt(1 - H_cam)
    float disk_gain;     // artistic display gain on disk brightness (not physical)
    float T_peak;        // display temperature of the hottest disk annulus [K]
    float exposure;      // tone-mapping exposure
    float cam_u[4];      // covariant 4-velocity of the static camera
    float cam_e[12];     // covariant orthonormal camera triad, cam_e[4*k + mu]: k=0 right, 1 up, 2 forward
    float cam_pos[3];    // camera position in Kerr-Schild Cartesian coordinates
    float tan_half_fov;  // tangent of half the vertical field of view
    float aspect;        // width / height of the render target
    uint  width;         // render target width in pixels
    uint  height;        // render target height in pixels
    int   lut_n;         // number of samples in the disk flux table
    int   max_steps;     // integration step budget per photon
    int   bg_mode;       // 0 = stars, 1 = latitude/longitude grid, 2 = both
    int   disk_on;       // 1 = draw the accretion disk
    int   pad0;
    int   pad1;
};

// Vertex of the rubber-sheet ("trapdoor") mesh.
struct FunnelVertex {
    float pos[4];        // world position xyz, w unused
    float par[4];        // r (BL radius), phi, unused, unused
};

// Uniforms for the rubber-sheet pass.
struct FunnelUniforms {
    float mvp[16];       // column-major model-view-projection (Metal depth range [0,1])
    float r_plus;
    float r_isco;
    float r_ph;          // prograde photon orbit radius
    float r_view;        // outer radius of the sheet
    float y_top;         // height of the rim (usually 0)
    float y_bottom;      // height of the throat (negative)
    float pad0;
    float pad1;
};

#endif
