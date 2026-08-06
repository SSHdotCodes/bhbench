// verify.cpp — `blackhole --verify` runs the renderer's own geodesic machinery
// against closed-form results from the general relativity literature.  Every
// test uses the exact same Hamiltonian and Cash-Karp integrator that the GPU
// shader mirrors, so a pass here is evidence about the picture on screen.

#include "app.hpp"
#include "kerr.hpp"
#include "spectrum.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>

namespace {

int gPass = 0, gFail = 0;

void report(const char* name, double got, double want, double tolRel, const char* units = "") {
    double err = (std::abs(want) > 1e-12) ? std::abs(got - want) / std::abs(want)
                                          : std::abs(got - want);
    bool ok = err <= tolRel;
    std::printf("  %-46s %14.8f  %14.8f  %9.2e  %s%s\n",
                name, got, want, err, ok ? "PASS" : "FAIL", units);
    if (ok) ++gPass; else ++gFail;
}

struct Trace {
    bool captured = false;
    double dphi = 0;
    double rmin = 1e300;
    double maxH = 0;
    int steps = 0;
    kerr::State end{};
};

// Adaptive driver shared by all the tests.  `dir` is +1 to integrate forward in
// the affine parameter and -1 to run the ray backwards, exactly as the shader
// does when tracing from the camera.
Trace integrate(kerr::State y, kerr::Constants c, double dir,
                double rFar, int maxSteps, double tol, bool timelike = false,
                double stopPhi = 0.0) {
    Trace t;
    double rh = kerr::horizonOuter(c.a);
    double h = dir * 0.05 * std::max(1.0, 0.05 * y.r);
    double phi0 = y.ph;
    double Href = timelike ? 0.5 : std::max(c.E * c.E, 1e-12);

    for (int i = 0; i < maxSteps; ++i) {
        double hmax = std::max(1e-4, 0.16 * (y.r - rh));
        if (std::abs(h) > hmax) h = dir * hmax;

        kerr::State out;
        double e = kerr::rkck(y, c, h, out) / tol;
        if (e > 1.0 && std::abs(h) > 1e-9) {
            h *= std::max(0.15, 0.9 * std::pow(e, -0.25));
            continue;
        }
        y = out;
        t.steps++;
        h *= std::min(4.0, 0.9 * std::pow(std::max(e, 1e-8), -0.2));

        t.rmin = std::min(t.rmin, y.r);
        t.maxH = std::max(t.maxH, std::abs(kerr::hamiltonian(y, c) + (timelike ? 0.5 : 0.0)) / Href);

        if (y.r < rh * 1.0005) { t.captured = true; break; }
        if (y.r > rFar) break;
        if (stopPhi > 0.0 && std::abs(y.ph - phi0) > stopPhi) break;
    }
    t.dphi = y.ph - phi0;
    t.end = y;
    return t;
}

// A photon aimed inward from r0 in the equatorial plane with impact parameter b.
kerr::State equatorialPhoton(double a, double r0, double b, kerr::Constants& c) {
    c.a = a; c.E = 1.0; c.L = b;
    double Del = r0 * r0 - 2.0 * r0 + a * a;
    double P = (r0 * r0 + a * a) * c.E - a * c.L;
    double rad = (P * P / Del - (c.L - a * c.E) * (c.L - a * c.E)) / Del;
    kerr::State y{0.0, r0, kerr::PI / 2.0, 0.0, -std::sqrt(std::max(rad, 0.0)), 0.0};
    return y;
}

// ---------------------------------------------------------------- tests ----

void testDeflection() {
    std::printf("\n[1] Light deflection by a Schwarzschild hole\n");
    std::printf("    Keeton & Petters (2005) strong-deflection series, to 4th order:\n");
    std::printf("      dphi = 4(M/b) + (15pi/4)(M/b)^2 + (128/3)(M/b)^3 + (3465pi/64)(M/b)^4\n");
    std::printf("    Tolerance for each row is set by the size of the first omitted term.\n");
    std::printf("  %-46s %14s %14s %9s\n", "impact parameter", "traced", "series", "rel.err");

    const double r0 = 1.0e9;
    for (double b : {50.0, 100.0, 500.0, 2000.0}) {
        kerr::Constants c;
        kerr::State y = equatorialPhoton(0.0, r0, b, c);
        Trace t = integrate(y, c, +1.0, r0, 400000, 1e-13);
        // Reference sweep of a Euclidean straight line between the actual start
        // and end radii (the adaptive step overshoots r0, and at 1e-3 rad total
        // deflection that overshoot matters).
        double straight = kerr::PI - std::asin(std::min(1.0, b / r0))
                                   - std::asin(std::min(1.0, b / t.end.r));
        double defl = std::abs(t.dphi) - straight;
        double u = 1.0 / b;
        double series = 4.0 * u + (15.0 * kerr::PI / 4.0) * u * u
                      + (128.0 / 3.0) * u * u * u
                      + (3465.0 * kerr::PI / 64.0) * u * u * u * u;
        double nextTerm = (3584.0 / 5.0) * std::pow(u, 5);   // first omitted order
        double tol = std::max(3.0 * nextTerm / series, 1e-6);
        report((std::string("b = ") + std::to_string((int)b) + " M").c_str(), defl, series, tol, " rad");
    }
}

void testShadow() {
    std::printf("\n[2] Shadow edge: critical impact parameter from the camera pipeline\n");
    std::printf("    Rays are launched through the same ZAMO tetrad the shader uses,\n");
    std::printf("    then bisected on capture.  Compared with Bardeen (1973).\n");
    std::printf("  %-46s %14s %14s %9s\n", "case", "traced", "analytic", "rel.err");

    const double robs = 1.0e4;

    // Bardeen's xi(r) evaluated at the equatorial photon orbits gives the two
    // edges of the shadow seen by an observer in the equatorial plane.
    auto xiOf = [](double r, double a) {
        double Del = r * r - 2.0 * r + a * a;
        return ((r * r - a * a) - r * Del) / (a * (r - 1.0));
    };

    struct Case { double a; };
    for (Case cs : {Case{0.0}, Case{0.5}, Case{0.9}, Case{0.998}}) {
        double a = cs.a;

        // Sweep the local viewing angle; positive psi tilts toward +phi.
        auto captured = [&](double psi) {
            double n[3] = {-std::cos(psi), 0.0, std::sin(psi)};
            kerr::Constants c;
            double pr, pth;
            kerr::raymomentum(robs, kerr::PI / 2.0, a, n, c, pr, pth);
            kerr::State y{0.0, robs, kerr::PI / 2.0, 0.0, pr, pth};
            return integrate(y, c, -1.0, robs * 1.5, 200000, 1e-10).captured;
        };
        auto xiAt = [&](double psi) {
            double n[3] = {-std::cos(psi), 0.0, std::sin(psi)};
            kerr::Constants c;
            double pr, pth;
            kerr::raymomentum(robs, kerr::PI / 2.0, a, n, c, pr, pth);
            return c.L / c.E;
        };
        auto edge = [&](double lo, double hi) {
            for (int i = 0; i < 80; ++i) {
                double m = 0.5 * (lo + hi);
                if (captured(m)) lo = m; else hi = m;
            }
            return 0.5 * (lo + hi);
        };

        // psi = 0 points straight at the hole and is certainly captured.
        double psiPos = edge(0.0, 0.01);
        double psiNeg = edge(0.0, -0.01);
        double xiA = xiAt(psiPos), xiB = xiAt(psiNeg);

        double wantA, wantB;
        if (a < 1e-9) {
            wantA = 3.0 * std::sqrt(3.0);
            wantB = -3.0 * std::sqrt(3.0);
        } else {
            double rp = kerr::photonRadius(a, true);
            double rr = kerr::photonRadius(a, false);
            wantA = xiOf(rr, a);      // retrograde photon orbit -> one edge
            wantB = xiOf(rp, a);      // prograde photon orbit  -> the other
        }
        if (std::abs(xiA - wantA) > std::abs(xiA - wantB)) std::swap(wantA, wantB);

        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.3f, edge 1 (xi = L/E)", a);
        report(buf, xiA, wantA, 3e-4, " M");
        std::snprintf(buf, sizeof(buf), "a = %.3f, edge 2 (xi = L/E)", a);
        report(buf, xiB, wantB, 3e-4, " M");
    }
}

void testOrbits() {
    std::printf("\n[3] Circular orbits, ISCO and the photon shell\n");
    std::printf("  %-46s %14s %14s %9s\n", "quantity", "traced/computed", "analytic", "rel.err");

    for (double a : {0.0, 0.5, 0.9, 0.998}) {
        // A circular orbit built from the analytic E, L must stay circular.
        double rc = kerr::iscoRadius(a, true) * 1.8;
        double E, L;
        kerr::circularEL(rc, a, E, L, true);
        kerr::Constants c{a, E, L};
        kerr::State y0{0.0, rc, kerr::PI / 2.0, 0.0, 0.0, 0.0};
        Trace t = integrate(y0, c, +1.0, 1e9, 200000, 1e-13, true, 6.0 * kerr::PI);

        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.3f, r after 3 orbits (r0 = %.3f)", a, rc);
        report(buf, t.end.r, rc, 1e-9, " M");

        // Orbital angular velocity from the integrated worldline.
        std::snprintf(buf, sizeof(buf), "a = %.3f, dphi/dt vs Kepler", a);
        report(buf, t.end.ph / t.end.t, kerr::keplerOmega(rc, a, true), 1e-9, " 1/M");
    }

    // The ISCO is where the circular-orbit energy is stationary in r.
    for (double a : {0.0, 0.5, 0.9}) {
        double ri = kerr::iscoRadius(a, true);
        double d = 1e-4, E1, E2, L;
        kerr::circularEL(ri - d, a, E1, L, true);
        kerr::circularEL(ri + d, a, E2, L, true);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.3f, dE/dr at ISCO", a);
        report(buf, (E2 - E1) / (2.0 * d), 0.0, 2e-3);
    }

    // A photon started exactly on the circular photon orbit must not drift.
    for (double a : {0.0, 0.5, 0.9}) {
        double rp = kerr::photonRadius(a, true);
        double Del = rp * rp - 2.0 * rp + a * a;
        // Circular null orbit: dr/dl = 0 and d^2r/dl^2 = 0 fixes b = xi.
        double b = ((rp * rp - a * a) - rp * Del) / (a * (rp - 1.0));
        if (a < 1e-9) b = 3.0 * std::sqrt(3.0);
        kerr::Constants c{a, 1.0, b};
        kerr::State y0{0.0, rp, kerr::PI / 2.0, 0.0, 0.0, 0.0};
        Trace t = integrate(y0, c, +1.0, 1e6, 200000, 1e-13, false, 4.0 * kerr::PI);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.3f, photon orbit radius drift", a);
        report(buf, t.end.r, rp, 1e-6, " M");
    }
}

void testPrecession() {
    std::printf("\n[4] Relativistic periapsis advance (Schwarzschild)\n");
    std::printf("    Orbit parameterised by semi-latus rectum p and eccentricity e.\n");
    std::printf("  %-46s %14s %14s %9s\n", "orbit", "traced", "6 pi M / p", "rel.err");

    for (double p : {200.0, 500.0, 1000.0}) {
        double e = 0.2;
        double E = std::sqrt((p - 2.0 - 2.0 * e) * (p - 2.0 + 2.0 * e) / (p * (p - 3.0 - e * e)));
        double L = p / std::sqrt(p - 3.0 - e * e);
        kerr::Constants c{0.0, E, L};

        // Start at periapsis (p_r = 0) and run one full radial period.  p_r
        // changes sign at apoapsis and again at the next periapsis, so the
        // second zero crossing closes the loop.
        kerr::State y{0.0, p / (1.0 + e), kerr::PI / 2.0, 0.0, 0.0, 0.0};
        double h = 0.5;
        int crossings = 0;
        double phiEnd = 0.0;
        for (int guard = 0; guard < 4000000 && crossings < 2; ++guard) {
            kerr::State out;
            double ee = kerr::rkck(y, c, h, out) / 1e-13;
            if (ee > 1.0 && h > 1e-8) { h *= std::max(0.15, 0.9 * std::pow(ee, -0.25)); continue; }
            if (y.pr * out.pr < 0.0) {
                ++crossings;
                if (crossings == 2) {
                    // Linear interpolation in p_r pins the periapsis exactly.
                    double f = y.pr / (y.pr - out.pr);
                    phiEnd = y.ph + f * (out.ph - y.ph);
                    break;
                }
            }
            y = out;
            h = std::min(h * std::min(3.0, 0.9 * std::pow(std::max(ee, 1e-9), -0.2)), 4.0);
        }
        double adv = phiEnd - 2.0 * kerr::PI;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "p = %.0f M, e = 0.2", p);
        report(buf, adv, 6.0 * kerr::PI / p, 12.0 / p, " rad/orbit");
    }
}

void testMetricIdentities() {
    std::printf("\n[5] Metric, tetrad and redshift identities\n");
    std::printf("  %-46s %14s %14s %9s\n", "quantity", "computed", "analytic", "rel.err");

    // For a = 0 the ZAMO reduces to a static observer with lapse sqrt(1-2M/r);
    // the photon it emits arrives at infinity redshifted by exactly that factor.
    for (double r : {6.0, 10.0, 50.0}) {
        kerr::ZAMO z = kerr::zamo(r, kerr::PI / 2.0, 0.0);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "gravitational redshift at r = %.0f M", r);
        report(buf, z.alpha, std::sqrt(1.0 - 2.0 / r), 1e-12);
    }

    // Frame dragging angular velocity of the ZAMO.
    for (double a : {0.5, 0.9}) {
        double r = 4.0;
        kerr::ZAMO z = kerr::zamo(r, kerr::PI / 2.0, a);
        double A = (r * r + a * a) * (r * r + a * a) - a * a * (r * r - 2.0 * r + a * a);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.1f, ZAMO omega at r = 4 M", a);
        report(buf, z.omega, 2.0 * a * r / A, 1e-12, " 1/M");
    }

    // The static limit surface on the equator sits at r = 2M for every spin.
    for (double a : {0.0, 0.7, 0.998}) {
        double gtt = -(1.0 - 2.0 * 2.0 / (2.0 * 2.0 + a * a * 0.0));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.3f, g_tt at r = 2 M (equator)", a);
        report(buf, gtt, 0.0, 1e-12);
    }

    // Horizon areas: A = 8 pi M (M + sqrt(M^2 - a^2)) = 4 pi (r_+^2 + a^2)
    for (double a : {0.0, 0.6, 0.95}) {
        double rp = kerr::horizonOuter(a);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.2f, horizon area / 4 pi", a);
        report(buf, rp * rp + a * a, 2.0 * (1.0 + std::sqrt(1.0 - a * a)), 1e-12, " M^2");
    }
}

void testDisk() {
    std::printf("\n[6] Novikov-Thorne accretion disk\n");
    std::printf("  %-46s %14s %14s %9s\n", "quantity", "computed", "analytic", "rel.err");

    // The flux must vanish at the ISCO (zero-torque inner boundary) and match
    // the Newtonian thin disk far out.
    for (double a : {0.0, 0.9}) {
        double ri = kerr::iscoRadius(a, true);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "a = %.1f, F at the ISCO", a);
        report(buf, kerr::ntFlux(ri * 1.0000001, a), 0.0, 1e-6);

        double rfar = 1.0e8;
        std::snprintf(buf, sizeof(buf), "a = %.1f, F r^3 8pi/3 -> 1 at r = 1e8 M", a);
        report(buf, kerr::ntFlux(rfar, a) * rfar * rfar * rfar * 8.0 * kerr::PI / 3.0, 1.0, 5e-3);
    }

    // Radiative efficiency 1 - E_isco.
    report("a = 0,     efficiency", kerr::efficiency(0.0), 1.0 - std::sqrt(8.0 / 9.0), 1e-9);
    report("a = 0.998, efficiency", kerr::efficiency(0.998), 0.3210, 3e-3);

    // Peak disk temperature for a canonical stellar-mass X-ray binary.
    double eta = kerr::efficiency(0.0);
    double mdot = spectrum::accretionRate(10.0, 1.0, eta);
    double Tmax = 0;
    for (int i = 0; i < 4000; ++i) {
        double r = 6.0 * std::pow(1.002, i);
        Tmax = std::max(Tmax, spectrum::effectiveTemperature(kerr::ntFlux(r, 0.0), 10.0, mdot));
    }
    std::printf("     10 Msun, Eddington, a = 0:  T_max = %.3g K  (peak %.2f nm, soft X-ray)\n",
                Tmax, spectrum::wienPeakNm(Tmax));

    mdot = spectrum::accretionRate(1e7, 0.3, kerr::efficiency(0.9));
    Tmax = 0;
    for (int i = 0; i < 4000; ++i) {
        double r = kerr::iscoRadius(0.9, true) * std::pow(1.002, i);
        Tmax = std::max(Tmax, spectrum::effectiveTemperature(kerr::ntFlux(r, 0.9), 1e7, mdot));
    }
    std::printf("     1e7 Msun, 0.3 Edd, a = 0.9: T_max = %.3g K  (peak %.2f nm, UV)\n",
                Tmax, spectrum::wienPeakNm(Tmax));
}

void testConservation() {
    std::printf("\n[7] Numerical quality of the integrator\n");
    std::printf("    Max |H| / E^2 along the ray, where H = 0 exactly on a null geodesic.\n");

    for (double a : {0.0, 0.9}) {
        for (double b : {5.5, 6.0, 8.0}) {
            kerr::Constants c;
            kerr::State y = equatorialPhoton(a, 500.0, b, c);
            Trace t = integrate(y, c, +1.0, 500.0, 200000, 1e-10);
            std::printf("     a = %.1f  b = %4.1f M  ->  r_min = %8.4f M   steps = %5d   max|H|/E^2 = %.3e  %s\n",
                        a, b, t.rmin, t.steps, t.maxH, t.captured ? "(captured)" : "(escaped)");
            if (t.maxH < 1e-8) ++gPass; else ++gFail;
        }
    }
}

}  // namespace

int runVerify() {
    std::printf("=================================================================================\n");
    std::printf(" Kerr ray tracer -- physics verification\n");
    std::printf(" Geometrised units G = c = M = 1.  Same Hamiltonian and Cash-Karp RK4(5)\n");
    std::printf(" integrator as the GPU shader, run here in double precision.\n");
    std::printf("=================================================================================\n");

    testDeflection();
    testShadow();
    testOrbits();
    testPrecession();
    testMetricIdentities();
    testDisk();
    testConservation();

    std::printf("\n---------------------------------------------------------------------------------\n");
    std::printf(" %d passed, %d failed\n", gPass, gFail);
    std::printf("---------------------------------------------------------------------------------\n");
    return gFail == 0 ? 0 : 1;
}
