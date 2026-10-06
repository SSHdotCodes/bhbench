// scene.h — application state, derived per-spin tables, and the per-frame uniform block.
#pragma once
#include <simd/simd.h>
#include <string>
#include <vector>

#include "physics.h"
#include "shaders/bh_shared.h"

enum CameraMotion { MOTION_HOVER = 0, MOTION_ORBIT = 1, MOTION_INFALL = 2 };

struct AppState {
    // black hole and disk
    double spin = 0.90;          // a/M (signed; < 0 ⇒ disk counter-rotates)
    double massSolar = 6.5e9;    // only sets physical units (time, accretion rate) in the HUD
    double Tpeak = 6000.0;       // rest-frame peak effective temperature of the disk [K]
    double rDiskOut = 20.0;      // outer disk radius [M]
    double turbAmp = 0.55;

    // camera: Boyer–Lindquist position + free look (degrees)
    double camR = 40.0, camTheta = 83.0, camPhi = 0.0;
    double yaw = 0.0, pitch = 0.0, fovDeg = 40.0;
    int motion = MOTION_HOVER;

    // toggles
    bool disk = true, gr = true, redshift = true, turbulence = true, limb = true;
    bool orderViz = false, shadowOverlay = false, bloom = true, sky = true, hud = true;
    bool paused = false, autoOrbit = false, plunging = false;
    int gridMode = BH_GRID_OFF;
    int tonemap = 0;             // 0 asinh (astro), 1 ACES filmic, 2 linear clip
    double exposureEV = 0.75;
    double stretch = 16.0;       // asinh softening
    double timeScale = 10.0;     // coordinate time [M] per wall-clock second
    double simTime = 0.0;        // coordinate time at the camera [M]

    // quality
    double stepScale = 1.0;      // integrator step multiplier (1 = validated production setting)
    int maxSteps = 1500;
};

struct SceneCache {
    double spin = -99, rDiskOut = -1;
    phys::DiskModel disk;
    phys::Funnel funnel;
    double funnelTop = 0.0, funnelScale = 1.0, funnelR1 = 34.0;
    std::vector<simd_float4> lut;
    double lutLogT0 = 2.0, lutLogT1 = 8.0;
    float special[BH_NUM_SPECIAL] = {};
    float specialFunnel[BH_NUM_SPECIAL] = {};
    int version = 0;             // bumps whenever the tables change (renderer re-uploads)
};

// Rebuild disk / funnel tables when the spin or disk size changed. Returns true if anything was rebuilt.
bool updateScene(const AppState& s, SceneCache& c);

struct CameraInfo {   // for the HUD
    double alpha = 1, omega = 0, velocity = 0, gamma = 1, redshiftSky = 1;
    bool insideErgosphere = false;
};

FrameUniforms buildUniforms(const AppState& s, const SceneCache& c, int width, int height, simd_float2 jitter,
                            CameraInfo* info = nullptr);

// Velocity (relative to the ZAMO) of a circular equatorial orbit at radius r; returns > 1 if none exists.
double orbitVelocity(double a, double r);

std::string hudText(const AppState& s, const SceneCache& c, const CameraInfo& ci, double fps, double gpuMs,
                    int w, int h, int accum);
std::string helpText();
