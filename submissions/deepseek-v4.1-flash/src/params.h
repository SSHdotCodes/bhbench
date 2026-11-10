// Shared simulation parameters, edited by the UI and consumed by the renderer.
#pragma once

struct Params {
    // --- camera (lensed view) ---
    float yaw = 0.62f;
    float pitch = 0.32f;
    float distance = 34.0f;
    float fovDeg = 50.0f;

    // --- camera (spacetime view) ---
    float gridYaw = 0.95f;
    float gridPitch = 0.46f;
    float gridDistance = 27.0f;
    float gridZScale = 1.5f;   // vertical exaggeration of the embedding diagram

    // --- accretion disk ---
    float diskInner = 6.0f;      // ISCO, in units of M
    float diskOuter = 18.0f;
    float diskTempPeak = 6000.0f;
    float diskBrightness = 1.0f;
    bool physical = false;       // absolute blackbody radiance vs normalised
    bool diskTexture = true;
    float diskCompress = 0.45f;  // display compression of the beaming contrast

    // --- sky ---
    bool showStars = true;
    float starBrightness = 1.0f;

    // --- equatorial coordinate grid (lensed) ---
    bool showGrid = false;
    float gridOuter = 42.0f;
    float gridIntensity = 1.0f;

    // --- post ---
    float exposure = 0.45f;
    float gamma = 1.0f;
    float bloom = 0.55f;
    float bloomThreshold = 0.75f;
    float vignette = 0.32f;

    // --- integration ---
    int maxSteps = 420;

    // --- view ---
    int mode = 0;  // 0 = lensed, 1 = spacetime, 2 = split

    // --- physical readouts ---
    double massSolar = 1e8;
    double eddington = 0.1;

    // --- time ---
    float timeScale = 1.0f;
    bool paused = false;

    // --- quality ---
    float renderScale = 0.85f;
    bool adaptive = true;
    bool hud = true;
};
