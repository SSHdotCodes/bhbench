#pragma once

// Shared geometric-unit constants for Schwarzschild spacetime (G = c = 1).
// Mass M is free; Schwarzschild radius rs = 2M.
// Photon sphere: r_ph = 1.5 rs = 3M
// ISCO (prograde, Schwarzschild): r_isco = 3 rs = 6M
// Marginally bound: r_mb = 2 rs = 4M

namespace bh {

constexpr float defaultMass = 1.0f;
constexpr float schwarzschildRadius(float M) { return 2.0f * M; }
constexpr float photonSphere(float M) { return 3.0f * M; }
constexpr float isco(float M) { return 6.0f * M; }

// Thin-disk model (Novikov–Thorne / Shakura–Sunyaev temperature proxy):
// effective temperature scales roughly as r^{-3/4} outside ISCO.
inline float diskTemperatureScale(float r, float rIsco) {
    if (r <= rIsco) return 0.0f;
    const float x = rIsco / r;
    // Softened profile peaking near ISCO and falling outward
    return x * x * x * (1.0f - __builtin_sqrtf(x)); // ~ r^{-3/4} family
}

} // namespace bh
