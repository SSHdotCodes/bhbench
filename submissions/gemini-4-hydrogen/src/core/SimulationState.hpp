#pragma once

#include "BlackHolePhysics.hpp"

namespace bh {

enum class CameraPreset {
    GargantuaInclined = 0,
    EdgeOnLensing = 1,
    PolarTopDown = 2,
    ClosePhotonSkim = 3
};

struct SimulationParams {
    double spinA = 0.90;            // Dimensionless Kerr spin a/M in [0, 0.998]
    double diskOuterR = 13.5;       // Accretion disk outer radius (in M)
    float diskBrightness = 0.95f;   // Relativistic emission multiplier
    float haloIntensity = 1.00f;    // Photon-sphere halo & corona mist
    float bloomIntensity = 0.42f;   // MPS Gaussian optical bloom strength
    float exposure = 1.05f;         // ACES Filmic exposure

    bool diskEnabled = true;
    bool dopplerEnabled = true;
    bool gridEnabled = true;
    bool infallParticlesEnabled = true;
    int backgroundMode = 0;         // 0 = Deep Space Milky Way + Stars, 1 = Einstein Ring Calibration Grid
    bool paused = false;

    float gridVerticalOffset = -2.4f; // Offset of asymptotic flat grid plane relative to z=0
    float gridOuterR = 28.0f;         // Outer radius of spacetime grid
    float gridOpacity = 0.90f;        // Opacity of the spacetime curvature trapdoor grid

    double simTime = 0.0;
    float timeScale = 1.0f;

    void resetDefaults() {
        *this = SimulationParams{};
    }
};

} // namespace bh
