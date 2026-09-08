#ifndef SHADER_TYPES_H
#define SHADER_TYPES_H

#ifdef __METAL_VERSION__
#define SIMD_FLOAT2 float2
#define SIMD_FLOAT3 float3
#define SIMD_FLOAT4 float4
#define SIMD_INT int
#define SIMD_UINT uint
#else
#include <simd/simd.h>
#define SIMD_FLOAT2 simd_float2
#define SIMD_FLOAT3 simd_float3
#define SIMD_FLOAT4 simd_float4
#define SIMD_INT int
#define SIMD_UINT unsigned int
#endif

struct BlackHoleParams {
    SIMD_FLOAT3 camPos;          // Camera position in Kerr/Boyer-Lindquist coordinates (x, y, z)
    float mass;                  // Mass M of the black hole (default 1.0)
    
    SIMD_FLOAT3 camForward;      // Normalized camera look direction
    float spin;                  // Kerr dimensionless spin parameter a (in [0, 0.998])
    
    SIMD_FLOAT3 camUp;           // Normalized camera up vector
    float rH;                    // Event horizon radius r_+ = M + sqrt(M^2 - a^2)
    
    SIMD_FLOAT3 camRight;        // Normalized camera right vector
    float rISCO;                 // Innermost Stable Circular Orbit radius
    
    SIMD_FLOAT2 resolution;      // Screen width, height
    float fov;                   // Vertical Field of View (radians)
    float time;                  // Elapsed time for animations / disk dynamic rotations
    
    // Accretion disk settings
    float diskInner;             // Disk inner boundary (e.g. rISCO)
    float diskOuter;             // Disk outer boundary (e.g. 14.0 * M)
    float diskAlpha;             // Disk optical density scaling
    float diskTempScale;         // Disk temperature / brightness scale
    
    // Spacetime curvature visualizer settings
    SIMD_INT renderGrid;         // 1 = show curved spacetime embedding grid, 0 = hide
    SIMD_INT renderDisk;         // 1 = show accretion disk, 0 = hide
    SIMD_INT renderLensing;      // 1 = full geodesic raytracing, 0 = flat space reference
    float gridHeightOffset;      // Visual vertical offset for Flamm's paraboloid grid
    
    float stepSizeScale;         // Integration precision / step scale (adaptive RK4)
    float maxSteps;              // Max geodesic integration steps
    SIMD_INT colorMode;          // 0 = Realistic Doppler/Grav redshift, 1 = Temperature false color, 2 = Gravitational redshift map
    float padding;
};

#endif // SHADER_TYPES_H
