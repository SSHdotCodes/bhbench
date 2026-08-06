#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

constexpr double kSchwarzschildRadius = 1.0;
constexpr double kMass = 0.5;  // G = c = 1, therefore r_s = 2M.
constexpr double kPhotonSphere = 1.5;
const double kCriticalImpact = 3.0 * std::sqrt(3.0) * kMass;

bool approximately(double actual, double expected, double tolerance) {
    if (std::abs(actual - expected) <= tolerance) {
        return true;
    }
    std::cerr << "Expected " << expected << ", got " << actual << '\n';
    return false;
}

// Integrates the exact Schwarzschild null radial equation using velocity Verlet.
// Returns true for capture and false for a ray scattered back beyond its start.
bool isCaptured(double impactParameter) {
    constexpr double startRadius = 50.0;
    constexpr double step = 0.0025;
    constexpr int maxSteps = 2'000'000;

    const double lapse = 1.0 - kSchwarzschildRadius / startRadius;
    const double energy = std::sqrt(lapse);
    const double angularMomentum = impactParameter * energy;
    double radius = startRadius;
    double radialVelocity = -std::sqrt(
        energy * energy - lapse * angularMomentum * angularMomentum / (radius * radius));

    auto acceleration = [angularMomentum](double r) {
        return angularMomentum * angularMomentum / (r * r * r)
             * (1.0 - 3.0 * kMass / r);
    };

    for (int i = 0; i < maxSteps; ++i) {
        const double a0 = acceleration(radius);
        const double nextRadius = radius + radialVelocity * step + 0.5 * a0 * step * step;
        if (nextRadius <= 1.0001) {
            return true;
        }
        const double a1 = acceleration(nextRadius);
        radialVelocity += 0.5 * (a0 + a1) * step;
        radius = nextRadius;
        if (radialVelocity > 0.0 && radius >= startRadius) {
            return false;
        }
    }
    return radius < kPhotonSphere;
}

}  // namespace

int main() {
    bool passed = true;

    // d2r/dlambda2 vanishes for the circular photon orbit at r = 3M.
    const double circularAcceleration =
        1.0 / std::pow(kPhotonSphere, 3.0) * (1.0 - 3.0 * kMass / kPhotonSphere);
    passed &= approximately(circularAcceleration, 0.0, 1e-14);

    // b_crit = r_photon / sqrt(1-r_s/r_photon) = 3*sqrt(3)M.
    const double geometricCritical =
        kPhotonSphere / std::sqrt(1.0 - kSchwarzschildRadius / kPhotonSphere);
    passed &= approximately(geometricCritical, kCriticalImpact, 1e-14);

    // Rays on opposite sides of the theoretical separatrix must capture/scatter.
    if (!isCaptured(kCriticalImpact * 0.99)) {
        std::cerr << "Subcritical ray should be captured\n";
        passed = false;
    }
    if (isCaptured(kCriticalImpact * 1.01)) {
        std::cerr << "Supercritical ray should scatter\n";
        passed = false;
    }

    if (!passed) {
        return EXIT_FAILURE;
    }
    std::cout << "Schwarzschild geodesic invariants and capture boundary: PASS\n";
    return EXIT_SUCCESS;
}
