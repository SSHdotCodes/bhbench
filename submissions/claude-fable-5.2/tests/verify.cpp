// =============================================================================
//  verify.cpp — checks the physics against analytic general relativity.
//
//  The geodesic integrator that runs on the GPU (shaders/kerr_geodesic.glsl)
//  is compiled here as C++, twice: once in float64 and once with a wrapper
//  type that forces every operation to float32 like the GPU.  The tests then
//  compare it with closed-form results: photon-orbit impact parameters, the
//  exact light-deflection integral, Bardeen's shadow curve, the redshift of
//  circular orbits, conserved quantities, and so on.  CPU-side tables
//  (Novikov-Thorne flux, embedding diagram, blackbody colours) are checked too.
//
//  Usage:  verify [eps]      (eps = integrator step fraction, default 0.2)
// =============================================================================
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

#include "../src/physics.hpp"
#include "../src/blackbody.hpp"

static int gPass = 0, gFail = 0;

static void report(bool ok, const char* name, const char* detail) {
    std::printf("  [%s] %-66s %s\n", ok ? " ok " : "FAIL", name, detail);
    (ok ? gPass : gFail)++;
}
static void check(const char* name, double value, double expected, double relTol) {
    char d[128];
    double err = std::fabs(value - expected) / std::max(1e-300, std::fabs(expected));
    std::snprintf(d, sizeof d, "%.6f vs %.6f  (rel err %.1e)", value, expected, err);
    report(err <= relTol, name, d);
}
static void checkBelow(const char* name, double value, double bound) {
    char d[128];
    std::snprintf(d, sizeof d, "%.2e  (< %.1e)", value, bound);
    report(value <= bound && value == value, name, d);
}
static void checkTrue(const char* name, bool ok) { report(ok, name, ""); }

// Scalar that performs every operation in IEEE single precision.
struct F32 {
    float v;
    F32() : v(0.0f) {}
    F32(double d) : v(float(d)) {}
    F32(float f) : v(f) {}
    F32(int i) : v(float(i)) {}
};
inline F32 operator+(F32 a, F32 b) { return F32(a.v + b.v); }
inline F32 operator-(F32 a, F32 b) { return F32(a.v - b.v); }
inline F32 operator*(F32 a, F32 b) { return F32(a.v * b.v); }
inline F32 operator/(F32 a, F32 b) { return F32(a.v / b.v); }
inline F32 operator-(F32 a) { return F32(-a.v); }
inline bool operator<(F32 a, F32 b) { return a.v < b.v; }
inline bool operator>(F32 a, F32 b) { return a.v > b.v; }
inline bool operator<=(F32 a, F32 b) { return a.v <= b.v; }
inline bool operator>=(F32 a, F32 b) { return a.v >= b.v; }

namespace f64 {
typedef double real;
inline double toDouble(double x) { return x; }
#define float double
#include "glsl_compat.inl"
#include "../shaders/kerr_geodesic.glsl"
#undef float
#include "verify_tests.inl"
}  // namespace f64

namespace f32 {
typedef F32 real;
inline double toDouble(F32 x) { return double(x.v); }
#define float F32
#include "glsl_compat.inl"
#include "../shaders/kerr_geodesic.glsl"
#undef float
#include "verify_tests.inl"
}  // namespace f32

// -----------------------------------------------------------------------------
static void testCpuPhysics() {
    std::printf("\n== CPU-side Kerr quantities (double) ==\n");
    check("horizon radius, a = 0", kerr::horizon(0.0), 2.0, 1e-12);
    check("ISCO, a = 0", kerr::isco(0.0), 6.0, 1e-12);
    check("ISCO, a -> +1 (prograde)", kerr::isco(1.0 - 1e-12), 1.0, 1e-3);
    check("ISCO, a -> -1 (retrograde)", kerr::isco(-1.0 + 1e-12), 9.0, 1e-6);
    check("ISCO, a = 0.998 (Thorne limit)", kerr::isco(0.998), 1.2370, 1e-4);
    check("photon orbit, a = 0", kerr::photonOrbit(0.0, true), 3.0, 1e-12);
    check("photon orbit, a -> 1 retrograde", kerr::photonOrbit(1.0, false), 4.0, 1e-12);
    check("binding energy at ISCO, a = 0  (sqrt(8/9))", kerr::circE(6.0, 0.0), std::sqrt(8.0 / 9.0), 1e-12);
    check("radiative efficiency 1-E_isco, a = 0.998", 1.0 - kerr::circE(kerr::isco(0.998), 0.998), 0.3212, 2e-3);

    // tetrad orthonormality
    {
        double X[3] = {5.0, -3.0, 2.0}, F[3] = {-0.8, 0.5, -0.3}, R[3] = {0.5, 0.8, 0.1}, U[3] = {0.1, 0.2, 0.9};
        kerr::Tetrad T;
        kerr::staticTetrad(X, 0.9, F, R, U, T);
        double g[4][4];
        kerr::ksMetric(X, 0.9, g);
        double worst = 0;
        for (int A = 0; A < 4; ++A)
            for (int B = 0; B < 4; ++B) {
                double s = 0;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) s += g[i][j] * T.e[A][i] * T.e[B][j];
                double eta = (A == B) ? (A == 0 ? -1.0 : 1.0) : 0.0;
                worst = std::max(worst, std::fabs(s - eta));
            }
        checkBelow("camera tetrad orthonormal in the Kerr-Schild metric", worst, 1e-12);
    }

    std::printf("\n== Novikov-Thorne disk flux: quadrature vs Page & Thorne (1974) closed form ==\n");
    const double spins[5] = {0.0, 0.5, 0.9, 0.998, -0.7};
    for (double a : spins) {
        kerr::DiskFlux D;
        D.build(a, 120.0);
        double worst = 0;
        for (double r = D.rIn * 1.05; r < 100.0; r *= 1.17) {
            double f1 = D.flux(r), f2 = kerr::fluxPageThorne(r, a);
            worst = std::max(worst, std::fabs(f1 / f2 - 1.0));
        }
        char name[128];
        std::snprintf(name, sizeof name, "a = %+.3f  (ISCO %.4f M, flux peak at %.3f M)", a, D.rIn, D.rPeak);
        checkBelow(name, worst, 2e-4);
    }
    {
        kerr::DiskFlux D;
        D.build(0.0, 4000.0, 40000);
        double r = 3000.0;
        double newton = 3.0 / (8.0 * kerr::kPi * r * r * r) * (1.0 - std::sqrt(6.0 / r));
        check("tends to Newtonian 3 M Mdot (1 - sqrt(r_in/r)) / (8 pi r^3) at 3000 M", D.flux(r) / newton, 1.0, 5e-2);
    }

    std::printf("\n== embedding diagram of the equatorial plane ==\n");
    {
        auto e = kerr::embedding(0.0, 40.0, 400);
        double worst = 0;
        for (auto& s : e) worst = std::max(worst, std::fabs(s.z - std::sqrt(8.0 * (s.r - 2.0))));
        checkBelow("a = 0 reproduces Flamm's paraboloid z = sqrt(8M(r - 2M))", worst, 1e-7);
        double tauExact = (2.0 / 3.0) * (std::pow(40.0, 1.5) - std::pow(2.0, 1.5)) / std::sqrt(2.0);
        check("rain proper time from r = 40 M to the horizon", e.back().tau, tauExact, 1e-8);
        // proper distance horizon -> r in Schwarzschild: sqrt(r(r-2)) + 2 ln( sqrt(r/2) + sqrt(r/2 - 1) )
        double ellExact = std::sqrt(40.0 * 38.0) + 2.0 * std::log(std::sqrt(20.0) + std::sqrt(19.0));
        check("proper radial distance from the horizon to r = 40 M", e.back().ell, ellExact, 1e-8);
        check("lapse at r = 40 M  (sqrt(1 - 2M/r))", e.back().lapse, std::sqrt(1.0 - 2.0 / 40.0), 1e-12);
        auto k = kerr::embedding(0.95, 40.0, 400);
        check("Kerr horizon circumference / 2 pi = 2M for any spin", k[0].Rc, 2.0, 1e-12);
        checkTrue("Kerr throat is deeper than Schwarzschild's", k.back().z > e.back().z);
    }

    std::printf("\n== blackbody colours ==\n");
    {
        auto chroma = [](double T, double& x, double& y) {
            double X, Y, Z;
            blackbody::xyzOf(T, X, Y, Z);
            x = X / (X + Y + Z); y = Y / (X + Y + Z);
        };
        double x, y;
        chroma(6500.0, x, y);
        checkBelow("Planckian locus at 6500 K: |xy - (0.3135, 0.3237)|", std::hypot(x - 0.3135, y - 0.3237), 4e-3);
        chroma(2856.0, x, y);
        checkBelow("Planckian locus at 2856 K: |xy - (0.4476, 0.4074)|", std::hypot(x - 0.4476, y - 0.4074), 4e-3);
        chroma(10000.0, x, y);
        checkBelow("Planckian locus at 10000 K: |xy - (0.2807, 0.2884)|", std::hypot(x - 0.2807, y - 0.2884), 4e-3);
    }
}

int main(int argc, char** argv) {
    double eps = argc > 1 ? std::atof(argv[1]) : 0.2;
    std::printf("Kerr black hole renderer - physics verification\n");
    testCpuPhysics();
    f64::runAll("float64", eps, 1.0);
    f32::runAll("float32 (GPU precision)", eps, 1.0);
    std::printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
