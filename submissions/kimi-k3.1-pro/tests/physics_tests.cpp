#include "../src/physics.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
        ++failures;
    }
}

enum class Fate { Captured, Escaped, StepLimit };

Fate trace(double radius, double impact, double* totalAngle = nullptr) {
    const double sinAlpha = impact * std::sqrt(bh::lapse(radius)) / radius;
    const double nRadial = -std::sqrt(1.0 - sinAlpha * sinAlpha);
    bh::OrbitState s = bh::initialOrbit(radius, nRadial, sinAlpha);
    double phi = 0.0;
    for (int i = 0; i < 200000; ++i) {
        const double h = 0.002;
        const double previousU = s.u;
        s = bh::rk4(s, h);
        phi += h;
        if (s.u >= 0.5) return Fate::Captured;
        if (phi > 1.0 && s.up < 0.0 && s.u <= 1.0 / radius) {
            if (totalAngle) {
                const double fraction = (previousU - 1.0 / radius) / (previousU - s.u);
                *totalAngle = phi - h + fraction * h;
            }
            return Fate::Escaped;
        }
    }
    return Fate::StepLimit;
}
} // namespace

int main() {
    near(bh::orbitAcceleration(1.0 / 3.0), 0.0, 1e-14,
         "photon sphere is a constant-u null orbit");
    near(bh::kCriticalImpact, 3.0 * std::sqrt(3.0), 1e-14,
         "critical impact parameter is 3 sqrt(3) M");
    near(bh::circularSpeed(6.0), 0.5, 1e-14,
         "ISCO circular speed is c/2 in the static frame");
    near(bh::circularGamma(6.0), 2.0 / std::sqrt(3.0), 1e-14,
         "ISCO circular Lorentz factor");
    near(std::sqrt(bh::lapse(6.0)), std::sqrt(2.0 / 3.0), 1e-14,
         "static gravitational redshift from r=6M to infinity");

    const double rMax = 49.0 / 36.0 * bh::kISCO;
    near(bh::diskFluxFactor(rMax), 46656.0 / 823543.0, 1e-14,
         "zero-torque disk flux peaks at (49/36) r_isco");

    const double testRadius = 50.0;
    const double testImpact = 5.4;
    const double testNT = testImpact * std::sqrt(bh::lapse(testRadius)) / testRadius;
    const double testNR = -std::sqrt(1.0 - testNT * testNT);
    const bh::OrbitState initial = bh::initialOrbit(testRadius, testNR, testNT);
    near(initial.up * initial.up + initial.u * initial.u * bh::lapse(testRadius),
         1.0 / (testImpact * testImpact), 1e-14,
         "initial direction satisfies the null conserved-energy invariant");

    expect(trace(50.0, 5.0) == Fate::Captured,
           "a subcritical ray is captured by the horizon");
    expect(trace(50.0, 5.4) == Fate::Escaped,
           "a supercritical ray escapes to infinity");

    double angle = 0.0;
    expect(trace(1000000000.0, 100.0, &angle) == Fate::Escaped,
           "weak-field test ray escapes");
    const double deflection = angle - 3.14159265358979323846;
    const double expectedDeflection = 4.0 / 100.0
        + 15.0 * 3.14159265358979323846 / (4.0 * 100.0 * 100.0)
        + 128.0 / (3.0 * 100.0 * 100.0 * 100.0);
    near(deflection, expectedDeflection, 1.0e-5,
         "light deflection agrees with the third-order weak-field expansion");

    if (failures != 0) {
        std::cerr << failures << " physics test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Schwarzschild physics tests passed\n";
    return EXIT_SUCCESS;
}
