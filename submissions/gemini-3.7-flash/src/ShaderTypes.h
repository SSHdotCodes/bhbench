#pragma once
#include <simd/simd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Uniform structure shared across C++ and Metal Shaders
struct SimulationUniforms {
    // Camera parameters
    vector_float3 camPos;          // Camera position in Boyer-Lindquist Cartesian (x, y, z) in units of r_g (or M)
    float fovY;                    // Field of view in radians
    vector_float3 camTarget;       // Target looking position
    float aspectRatio;             // Viewport aspect ratio
    vector_float3 camUp;           // Camera up vector
    float time;                    // Elapsed simulation time (seconds)
    
    // Black hole parameters
    float mass;                    // Black hole mass M (set to 1.0 standard gravitational units)
    float spin;                    // Dimensionless Kerr spin parameter a (-0.998 to +0.998)
    float horizonRadius;           // Outer event horizon radius r_+ = M + sqrt(M^2 - a^2)
    float iscoRadius;              // Innermost Stable Circular Orbit r_ISCO
    
    // Accretion Disk parameters
    float diskInnerRadius;         // Inner boundary of radiating disk (units of M)
    float diskOuterRadius;         // Outer boundary of radiating disk (units of M)
    float diskScaleHeight;         // Disk vertical scale height (H/R)
    float diskTemperatureBase;     // Base temperature parameter in Kelvin (e.g. 1e7 K)
    float diskDensity;             // Opacity / optical depth scale factor
    float diskSpeedMultiplier;     // Rotation animation speed factor
    float dopplerStrength;         // Beaming exponent strength (3.0 for standard monochromatic, 4.0 for bolometric)
    int diskTextureMode;           // 0 = Turbulent plasma, 1 = Multi-temperature blackbody, 2 = Iron K-alpha line
    
    // Halo / Corona parameters
    float haloIntensity;           // Corona / spherical halo glow emission intensity
    float haloRadius;              // Falloff scale radius of the corona
    float haloFalloff;             // Power law exponent for corona falloff
    int enableCorona;              // 1 = Corona enabled, 0 = disabled
    
    // Spacetime Curvature / Grid parameters
    int enableSpacetimeGrid;       // 1 = Show 3D spacetime trapdoor grid, 0 = disabled
    float gridSpacing;             // Coordinate grid spacing in M
    float gridThickness;           // Thickness of grid lines
    float gridDepthScale;          // Flamm paraboloid depth scaling factor
    float gridAlpha;               // Transparency of spacetime mesh
    int gridType;                  // 0 = Flamm 3D Paraboloid ("Trapdoor"), 1 = Equatorial warped metric grid, 2 = Dual-Funnel Wormhole/Trapdoor
    
    // Geodesic Integrator parameters
    int maxSteps;                  // Maximum RK4 integration steps (e.g., 200 - 600)
    float stepSize;                // Base step size (dt or dlambda)
    float adaptiveStepFactor;      // Scaling for adaptive step near strong gravity
    float escapeRadius;            // Radius at which rays are considered escaped to celestial sphere (e.g. 50.0 M)
    
    // Rendering & Post-Processing
    int renderMode;                // 0 = Full Relativistic Ray Traced, 1 = Spacetime Grid Analysis, 2 = Redshift / Doppler Map, 3 = Gravitational Lensing Deflection Map
    float exposure;                // Exposure control for HDR tone mapping
    float bloomThreshold;          // Brightness threshold for bloom / glow
    float bloomIntensity;          // Bloom blending weight
    int enableSkybox;              // 1 = Realistic Milky Way & Starfield, 0 = Flat / dark
    int celestialLensingMode;       // 0 = Starfield + Milky Way, 1 = Pure Coordinate Grid on Sphere
    
    // Screen resolution
    vector_uint2 screenResolution;
    
    // Viewport and ray tracing quality settings
    int qualityLevel;              // 0 = Performance, 1 = Balanced, 2 = Ultra
    int showPhotonRingGlow;        // 1 = Emphasize infinite photon sub-rings (n=1, n=2...), 0 = Natural
    float reserved[6];             // Alignment padding for Metal / 16-byte boundary
};

#ifdef __cplusplus
}
#endif
