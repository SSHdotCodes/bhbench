// Physics verification of the geodesic integrator.
// Every test asserts a known analytic result of Schwarzschild spacetime.
#include <cstdio>
#include <cmath>
#include "geodesic.hpp"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("  PASS  %s\n", msg); \
    else { std::printf("  FAIL  %s\n", msg); ++failures; } \
} while (0)

static double norm(const double v[3]) {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

// 1. Radial photon: falls to the horizon in finite affine parameter.
static void testRadialInfall() {
    std::printf("[1] radial photon infall\n");
    double x[3] = {20, 0, 0}, p[3] = {-1, 0, 0};
    geo::project(p, x, 1.0, 0.0);
    double rmin = 1e9;
    for (int i = 0; i < 200000; ++i) {
        double r = norm(x);
        rmin = std::min(rmin, r);
        if (r <= geo::RS * 1.0005) break;
        geo::rk4step(x, p, 1.0, 0.0, geo::stepSize(r, 0.01, 0.002, 0.3));
        if (i == 199999) rmin = 1e9;
    }
    CHECK(rmin < 2.001, "photon crosses r < 2.001 (horizon)");
    // null constraint drift
    double pxh = p[0]*x[0] + p[1]*x[1] + p[2]*x[2];
    double r = norm(x);
    double null2 = -1.0/geo::lapse(r) + (p[0]*p[0]+p[1]*p[1]+p[2]*p[2]) - geo::RS*pxh*pxh/(r*r*r);
    CHECK(std::fabs(null2) < 1e-6, "null constraint |p|^2 - (rs/r)(p.xhat)^2 = E^2/A holds");
}

// 2. Photon sphere: photon launched tangentially at r = 3M + eps orbits.
static void testPhotonSphere() {
    std::printf("[2] photon sphere at r = 3M\n");
    double x[3] = {3.0001, 0, 0}, p[3] = {0, 1, 0}; // tangential, E = 1
    geo::project(p, x, 1.0, 0.0);
    double rmax = 0, rmin = 1e9;
    double a0 = std::atan2(x[1], x[0]);
    for (int i = 0; i < 40000; ++i) {
        double r = norm(x);
        rmax = std::max(rmax, r); rmin = std::min(rmin, r);
        if (r <= geo::RS * 1.0005 || r > geo::ESC_R) break;
        geo::rk4step(x, p, 1.0, 0.0, geo::stepSize(r, 0.004, 0.0005, 0.05));
        if (i == 39999) a0 = -1; // mark as not completed
    }
    double a1 = std::atan2(x[1], x[0]);
    CHECK(a0 >= 0 && std::fabs(rmax - 3.0001) < 0.02 && rmin > 2.9,
          "photon orbits r ~ 3M (unstable circular orbit)");
    double orbits = (a1 > a0) ? (a1 - a0) / (2*M_PI) : (a1 - a0 + 2*M_PI) / (2*M_PI);
    std::printf("       %.1f orbits completed, r in [%.4f, %.4f]\n", orbits, rmin, rmax);
}

// 3. Shadow: impact parameter threshold at 3*sqrt(3)*M ~ 5.196.
static void testShadowRadius() {
    std::printf("[3] shadow radius 3 sqrt(3) M = 5.196\n");
    double lo = 0, hi = 6.0; // capture threshold bracketing
    for (int iter = 0; iter < 60; ++iter) {
        double b = 0.5 * (lo + hi);
        double x[3] = {50, b, 0}, p[3] = {-1, 0, 0};
        bool captured = false;
        for (int i = 0; i < 400000; ++i) {
            double r = norm(x);
            if (r <= geo::RS * 1.0005) { captured = true; break; }
            if (r > 51) break;
            geo::rk4step(x, p, 1.0, 0.0, geo::stepSize(r, 0.01, 0.002, 0.3));
        }
        if (captured) lo = b; else hi = b;
    }
    double bcrit = 0.5 * (lo + hi);
    CHECK(std::fabs(bcrit - 3.0 * std::sqrt(3.0)) < 0.01,
          "critical impact parameter = 5.196 (shadow edge)");
    std::printf("       measured b_crit = %.4f (theory %.4f)\n", bcrit, 3.0 * std::sqrt(3.0));
}

// 4. Weak-field deflection: delta = 4M/b (Euler/Maxwell limit).
static void testDeflection() {
    std::printf("[4] weak-field deflection angle 4M/b\n");
    double b = 30.0;
    double x[3] = {-60, b, 0}, p[3] = {1, 0, 0};
    double din[3] = {1, 0, 0};
    double dout[3];
    for (int i = 0; i < 400000; ++i) {
        double r = norm(x);
        if (r > 60) { dout[0]=p[0]; dout[1]=p[1]; dout[2]=p[2];
                      double n = norm(dout);
                      for (double& c : dout) c /= n;
                      break; }
        geo::rk4step(x, p, 1.0, 0.0, geo::stepSize(r, 0.05, 0.01, 0.5));
    }
    double dotp = dout[0]*din[0] + dout[1]*din[1] + dout[2]*din[2];
    double delta = std::acos(dotp);
    double theory = 4.0 / b; // 4M/b, M = 1
    CHECK(std::fabs(delta - theory) < 0.01 * theory + 1e-4,
          "delta = 4M/b for b >> rs");
    std::printf("       measured %.5f rad, theory %.5f rad\n", delta, theory);
}

// 5. ISCO: circular timelike orbit at r = 6M is stable.
static void testISCOOrbit() {
    std::printf("[5] circular timelike orbit at r = 6M (ISCO)\n");
    double r0 = 6.0;
    double L = std::sqrt(r0 * r0 * 1.0 / (r0 - 3.0)); // p_phi = sqrt(M r^2/(r-3M))
    double E = (1.0 - 2.0 / r0) / std::sqrt(1.0 - 3.0 / r0);
    double x[3] = {r0, 0, 0}, p[3] = {0, L / r0, 0};
    geo::project(p, x, E, 1.0);
    double drmax = 0;
    for (int i = 0; i < 200000; ++i) {
        double r = norm(x);
        drmax = std::max(drmax, std::fabs(r - r0));
        geo::rk4step(x, p, E, 1.0, 0.02);
        if (r < 2.0 || r > 12) break;
    }
    CHECK(drmax < 1e-3, "orbit stays at r = 6M");
    std::printf("       max |dr| = %.2e\n", drmax);
}

// 6. Plunging orbit below ISCO spirals into the horizon.
static void testPlunge() {
    std::printf("[6] particle with L < L_ISCO plunges\n");
    double x[3] = {6.0, 0, 0};
    double L = std::sqrt(36.0 / 3.0) * 0.98; // 2% below circular
    double E = (1.0 - 2.0 / 6.0) / std::sqrt(1.0 - 3.0 / 6.0) * 0.999;
    double p[3] = {0, L / 6.0, 0};
    geo::project(p, x, E, 1.0);
    bool crossed = false;
    for (int i = 0; i < 400000 && !crossed; ++i) {
        double r = norm(x);
        if (r <= geo::RS * 1.0005) { crossed = true; break; }
        geo::rk4step(x, p, E, 1.0, 0.02);
    }
    CHECK(crossed, "sub-ISCO particle reaches the horizon");
}

int main() {
    std::printf("Schwarzschild geodesic verification\n");
    std::printf("===================================\n");
    testRadialInfall();
    testPhotonSphere();
    testShadowRadius();
    testDeflection();
    testISCOOrbit();
    testPlunge();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
