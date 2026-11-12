#include "../src/core/BlackHolePhysics.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>

using namespace bh::physics;

namespace {

constexpr double kPi = 3.14159265358979323846;

#define REQUIRE(cond)                                                               \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << "[FAIL] " << #cond << " at line " << __LINE__ << "\n";     \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

void checkNear(double actual, double expected, double tol, const char* name) {
    double err = std::abs(actual - expected);
    if (err > tol) {
        std::cerr << "[FAIL] " << name << ": actual=" << actual
                  << " expected=" << expected << " err=" << err << " > tol=" << tol << "\n";
        std::exit(1);
    }
}

void testAnalyticalRadii() {
    checkNear(eventHorizonRadius(0.0), 2.0, 1e-12, "Schwarzschild horizon r_+");
    checkNear(iscoRadius(0.0), 6.0, 1e-12, "Schwarzschild ISCO");
    checkNear(photonSpherePrograde(0.0), 3.0, 1e-12, "Schwarzschild photon sphere");
    checkNear(photonSphereRetrograde(0.0), 3.0, 1e-12, "Schwarzschild retrograde photon sphere");

    double a = 0.998;
    double rp = eventHorizonRadius(a);
    double rISCO = iscoRadius(a);
    double rPhPro = photonSpherePrograde(a);
    double rPhRet = photonSphereRetrograde(a);
    REQUIRE(rp > 1.05 && rp < 1.07);
    REQUIRE(rISCO > 1.20 && rISCO < 1.30);
    REQUIRE(rPhPro < 1.15 && rPhPro > rp);
    REQUIRE(rPhRet > 3.9 && rPhRet < 4.01);
    std::cout << "[PASS] Analytical Kerr horizon, photon sphere & ISCO radii\n";
}

void testFlammParaboloid() {
    double rOuter = 28.0;
    for (double r : {2.0, 2.5, 4.0, 6.0, 12.0, 20.0, 28.0}) {
        double numZ = kerrEquatorialEmbeddingZ(r, 0.0, rOuter);
        double exactZ = 2.0 * std::sqrt(2.0 * (r - 2.0)) - 2.0 * std::sqrt(2.0 * (rOuter - 2.0));
        checkNear(numZ, exactZ, 1e-6, "Flamm paraboloid exact match at a=0");
    }
    // Check Kerr embedding is smooth and negative inside rOuter
    double kerrZ = kerrEquatorialEmbeddingZ(eventHorizonRadius(0.9), 0.9, rOuter);
    REQUIRE(kerrZ < -5.0 && std::isfinite(kerrZ));
    std::cout << "[PASS] Flamm paraboloid & Kerr equatorial spatial embedding\n";
}

// Compute ray direction from camera at (r0, theta0=pi/2, phi0=0) corresponding to asymptotic impact parameter b
void shootEquatorialImpactParam(double r0, double bSigned, double a, RayTraceResult& out) {
    // At large r0, local sin(angle) ~ b / r0 (using exact Schwarzschild/ZAMO relation Lz = -r0 * nphi)
    // Wait: at r0, nphi = -bSigned / varpi (with E ~ alpha), let's aim directly with angle alpha_cam = asin(bSigned / r0)
    double sinA = std::clamp(bSigned / r0, -0.999, 0.999);
    double cosA = std::sqrt(1.0 - sinA * sinA);
    double nr = -cosA;     // Looking inward toward black hole
    double ntheta = 0.0;
    double nphi = -sinA;   // Positive bSigned -> prograde (Lz > 0)
    out = traceGeodesicCPU(r0, kPi * 0.5, 0.0, nr, ntheta, nphi, a, 3000);
}

void testSchwarzschildCriticalCurve() {
    // Exact Schwarzschild critical impact parameter bc = 3*sqrt(3) = 5.1961524227...
    // From finite r0 = 60, asymptotic b corresponds to local sinA = bc * sqrt(1 - 2/r0) / r0
    double r0 = 60.0;
    double lapse = std::sqrt(1.0 - 2.0 / r0);
    double bc = 3.0 * std::sqrt(3.0);

    auto traceAsymptoticB = [&](double bTarget) {
        double sinA = (bTarget * lapse) / r0;
        double cosA = std::sqrt(1.0 - sinA * sinA);
        return traceGeodesicCPU(r0, kPi * 0.5, 0.0, -cosA, 0.0, -sinA, 0.0, 4000);
    };

    RayTraceResult captured = traceAsymptoticB(bc - 0.02);
    RayTraceResult escaped  = traceAsymptoticB(bc + 0.02);
    REQUIRE(captured.outcome == RayOutcome::Captured);
    REQUIRE(escaped.outcome == RayOutcome::Escaped);
    REQUIRE(captured.maxConstraintErrorU < 1e-6);
    REQUIRE(escaped.maxConstraintErrorU < 1e-6);
    std::cout << "[PASS] Schwarzschild critical impact parameter b_c = 3*sqrt(3) M ("
              << std::setprecision(5) << bc << ")\n";
}

void testEinsteinDeflection() {
    double r0 = 500.0;
    double b = 60.0;
    double lapse = std::sqrt(1.0 - 2.0 / r0);
    double sinA = (b * lapse) / r0;
    double cosA = std::sqrt(1.0 - sinA * sinA);

    RayTraceResult res = traceGeodesicCPU(r0, kPi * 0.5, 0.0, -cosA, 0.0, -sinA, 0.0, 4000);
    REQUIRE(res.outcome == RayOutcome::Escaped);

    // Starting at finite r0 with local inward angle alpha = asin(sinA),
    // a straight Euclidean ray reaches asymptotic azimuth -(pi - alpha).
    // Gravitational lensing deflects it further by 4M/b + 15*pi*M^2/(4*b^2).
    double alpha = std::asin(sinA);
    double deflection = std::abs(res.finalPhi) - (kPi - alpha);
    double analytic = 4.0 / b + (15.0 * kPi) / (4.0 * b * b); // 1PN + 2PN Einstein deflection
    checkNear(deflection, analytic, 0.001, "Einstein weak-field deflection 4M/b");
    std::cout << "[PASS] Weak-field gravitational lensing deflection: measured="
              << deflection << " rad vs GR=" << analytic << " rad\n";
}

void testKerrShadowAsymmetry() {
    double a = 0.95;
    double r0 = 50.0;

    // Find critical impact parameter on prograde side (b > 0 -> Lz > 0) vs retrograde side (b < 0 -> Lz < 0)
    auto findCriticalB = [&](double sign) {
        double lo = 1.0, hi = 9.0;
        for (int i = 0; i < 30; ++i) {
            double mid = 0.5 * (lo + hi);
            RayTraceResult r{};
            shootEquatorialImpactParam(r0, sign * mid, a, r);
            if (r.outcome == RayOutcome::Captured) lo = mid;
            else hi = mid;
        }
        return 0.5 * (lo + hi);
    };

    double bPro = findCriticalB(+1.0);
    double bRet = findCriticalB(-1.0);
    // For Kerr a=0.95, prograde critical impact parameter is ~2.8M, retrograde is ~6.8M
    REQUIRE(bPro < 3.5 && bRet > 6.2 && (bRet - bPro) > 3.0);
    std::cout << "[PASS] Kerr frame-dragging D-shadow asymmetry (a=" << a
              << "): b_prograde=" << bPro << " M, b_retrograde=" << bRet << " M\n";
}

void testPolarPassage() {
    // Shoot near-polar ray (theta0 = pi/2, nphi = 0, ntheta = -0.25 => b ~ 7.5M, escapes over north pole)
    double ntheta = -0.25;
    double nr = -std::sqrt(1.0 - ntheta * ntheta);
    RayTraceResult res = traceGeodesicCPU(30.0, kPi * 0.5, 0.0, nr, ntheta, 0.0, 0.85, 3000);
    REQUIRE(res.outcome == RayOutcome::Escaped);
    REQUIRE(res.maxConstraintErrorU < 1e-5);
    REQUIRE(res.maxConstraintErrorMu < 1e-5);
    std::cout << "[PASS] Near-polar Kerr geodesic integration & Hamiltonian constraint\n";
}

} // namespace

int main() {
    testAnalyticalRadii();
    testFlammParaboloid();
    testSchwarzschildCriticalCurve();
    testEinsteinDeflection();
    testKerrShadowAsymmetry();
    testPolarPassage();
    std::cout << "ALL GENERAL RELATIVITY PHYSICS TESTS PASSED.\n";
    return 0;
}
