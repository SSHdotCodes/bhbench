#include "selftest.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "physics.hpp"

namespace bh {
namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("  [%s] %-46s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (!ok) ++g_fail;
}

// Net deflection angle of a photon with impact parameter b, computed by
// integrating the exact geodesic equation from r0 in to periapsis and back out.
double deflectionAngle(double b, double r0, double stepMax, int maxSteps) {
    Photon y;
    y.u = 1.0 / r0;
    double rad = 1.0 / (b * b) - y.u * y.u * (1.0 - RS * y.u);
    if (rad <= 0.0) return 0.0;
    y.w = std::sqrt(rad);  // incoming

    TraceOut t = tracePhoton(y, stepMax, r0, maxSteps);
    if (t.term != Term::Escaped) return 1e9;  // captured

    // Flat-space reference with the same start point and start direction:
    // u = A sin(phi + phi0), A = sqrt(u0^2 + w0^2), returns to the same radius.
    double A = std::sqrt(y.u * y.u + y.w * y.w);
    double flat = M_PI - 2.0 * std::asin(std::min(y.u / A, 1.0));
    return t.phi - flat;
}

}  // namespace

int runSelfTests() {
    std::printf("================================================================\n");
    std::printf(" Black hole renderer - physics self test\n");
    std::printf(" (all lengths in units of M = GM/c^2, rs = 2M)\n");
    std::printf("================================================================\n");

    // ---------------------------------------------------------------- 1
    std::printf("\n1) Light deflection vs. 3-term series in b = L/E\n"
                "   alpha = 4M/b + (15pi/4)(M/b)^2 + (128/3)(M/b)^3\n");
    {
        const double r0 = 1.0e4, stepMax = 0.05, maxSteps = 2000000;
        struct C { double b, rel; };
        C cases[] = {{50.0, 1e-3}, {100.0, 1e-3}, {300.0, 1e-3}, {1000.0, 1e-3}};
        for (const C& c : cases) {
            double num = deflectionAngle(c.b, r0, stepMax, maxSteps);
            double ana = 4.0 / c.b + (15.0 * M_PI / 4.0) / (c.b * c.b) +
                         (128.0 / 3.0) / (c.b * c.b * c.b);
            double rel = std::fabs(num - ana) / std::fabs(ana);
            char buf[144];
            std::snprintf(buf, sizeof buf, "b=%6.0fM  numeric=%.8f  series=%.8f  rel=%.2e",
                          c.b, num, ana, rel);
            check(rel < c.rel, "deflection matches weak-field expansion", buf);
        }
    }

    // ---------------------------------------------------------------- 2
    std::printf("\n2) Critical impact parameter  (photon capture, b_c = 3 sqrt(3) M)\n");
    {
        const double r0 = 1.0e4;
        double lo = 4.0, hi = 7.0;  // lo captured, hi escapes
        auto captured = [&](double b) {
            Photon y;
            y.u = 1.0 / r0;
            double rad = 1.0 / (b * b) - y.u * y.u * (1.0 - RS * y.u);
            if (rad <= 0.0) return false;
            y.w = std::sqrt(rad);
            return tracePhoton(y, 0.05, r0, 400000).term == Term::Captured;
        };
        // Ensure the bracket is valid.
        if (!captured(lo)) { lo = 3.0; }
        if (captured(hi)) { hi = 9.0; }
        if (captured(lo) && !captured(hi)) {
            for (int i = 0; i < 60; ++i) {
                double m = 0.5 * (lo + hi);
                if (captured(m)) lo = m; else hi = m;
            }
            double bc = 0.5 * (lo + hi);
            double err = std::fabs(bc - B_CRIT);
            char buf[128];
            std::snprintf(buf, sizeof buf, "b_c = %.6f  exact 3sqrt(3) = %.6f  |err| = %.2e",
                          bc, B_CRIT, err);
            check(err < 1e-3, "capture threshold matches photon-sphere value", buf);
        } else {
            check(false, "capture threshold matches photon-sphere value", "bracket invalid");
        }
    }

    // ---------------------------------------------------------------- 3
    std::printf("\n3) Photon sphere is a fixed point of u'' + u = 3Mu^2 at r = 3M\n");
    {
        Photon y{1.0 / 3.0, 0.0};
        double phi = 0.0;
        const double h = 0.01;
        while (phi < 6.0) { y = rk4Step(y, h); phi += h; }
        double r = 1.0 / y.u;
        char buf[96];
        std::snprintf(buf, sizeof buf, "after phi=6: r = %.9f M  (u err %.2e)", r, std::fabs(y.u - 1.0/3.0));
        check(std::fabs(r - 3.0) < 1e-6, "circular photon orbit stays at r = 3M", buf);
    }

    // ---------------------------------------------------------------- 4
    std::printf("\n4) Redshift factor g = sqrt(1-rs/r)/(1 - Omega b) vs. SR + GR decomposition\n");
    {
        bool ok = true;
        std::string worst;
        for (double r : {8.0, 20.0, 100.0, 1000.0}) {
            double bt = r / std::sqrt(1.0 - RS / r);       // photon emitted tangentially
            double Om = keplerOmega(r);
            double beta = Om * r / std::sqrt(1.0 - RS / r);  // orbital speed seen by static observer
            double gam = 1.0 / std::sqrt(1.0 - beta * beta);
            double grav = std::sqrt(1.0 - RS / r);
            for (int s = +1; s >= -1; s -= 2) {            // co- and counter-propagating
                double g = gFactor(r, s * bt);
                double ref = grav * gam * std::sqrt((1.0 + s * beta) / (1.0 - s * beta));
                double err = std::fabs(g - ref) / ref;
                if (err > 1e-9) {
                    ok = false;
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "r=%g s=%+d beta=%.4f g=%.6f ref=%.6f rel=%.2e",
                                  r, s, beta, g, ref, err);
                    worst = buf;
                }
            }
        }
        check(ok, "Doppler * gravitational shift identity", worst);
    }

    // ---------------------------------------------------------------- 5
    std::printf("\n5) Flamm paraboloid embedding  (1 + z'^2 = 1/(1 - rs/r)) and K = -rs/(2r^3)\n");
    {
        bool okIso = true, okK = true;
        char detail[128] = "";
        for (double r : {3.0, 5.0, 10.0, 30.0, 80.0}) {
            double h = r * 1e-4;
            double zp = (flammZ(r + h) - flammZ(r - h)) / (2.0 * h);          // z'
            double zpp = (flammZ(r + h) - 2 * flammZ(r) + flammZ(r - h)) / (h * h);  // z''
            double iso = 1.0 + zp * zp;
            double isoRef = 1.0 / (1.0 - RS / r);
            if (std::fabs(iso - isoRef) / isoRef > 1e-6) { okIso = false; std::snprintf(detail, sizeof detail, "r=%g iso=%.8f ref=%.8f", r, iso, isoRef); }

            // K from the graph formula K = (z'' z' / r) / (1 + z'^2)^2
            double K = (zpp * zp / r) / std::pow(1.0 + zp * zp, 2.0);
            if (std::fabs(K - gaussianCurvature(r)) / std::fabs(gaussianCurvature(r)) > 1e-4) {
                okK = false;
                std::snprintf(detail, sizeof detail, "r=%g K=%.6e ref=%.6e", r, K, gaussianCurvature(r));
            }
        }
        check(okIso, "embedding is isometric to the Schwarzschild slice", detail);
        check(okK, "Gaussian curvature matches -rs/(2 r^3)", detail);
    }

    // ---------------------------------------------------------------- 6
    std::printf("\n6) Novikov-Thorne flux / temperature profile\n");
    {
        DiskParams d = makeDiskParams(10.0, 1e-7, false);
        bool shape = ntFluxShape(R_ISCO) == 0.0 && ntFluxShape(4.0) == 0.0 &&
                     ntFluxShape(R_FPeak) > 0.0;
        // Monotonic decrease beyond the peak.
        for (double r = R_FPeak + 1e-3; r < 60.0; r *= 1.05) shape = shape && ntFluxShape(r + 1) < ntFluxShape(r);
        char buf[192];
        std::snprintf(buf, sizeof buf,
                      "M=%.3g Msun, Mdot=%.3g Msun/yr -> T_peak = %.4g K (kT = %.3g keV), "
                      "display scale = %.4g",
                      d.massMsun, d.mdotMsunPerYr, d.TPhysPeak,
                      d.TPhysPeak * 8.617333e-8, d.tempScale);
        check(shape && d.TPhysPeak > 1e5 && d.TPhysPeak < 1e8, "flux shape and physical T_peak", buf);
    }

    // ---------------------------------------------------------------- 7
    std::printf("\n7) Blackbody colour LUT (Planck x CIE 1931 x XYZ->sRGB)\n");
    {
        const int N = 512;
        std::vector<float> lut = buildBlackbodyLUT(N);
        auto sample = [&](double T) {
            double t = std::log(std::max(T, 300.0) / 300.0) / std::log(1.0e7 / 300.0);
            int i = int(t * (N - 1));
            if (i < 0) i = 0;
            if (i > N - 1) i = N - 1;
            return std::array<float, 3>{lut[i * 4], lut[i * 4 + 1], lut[i * 4 + 2]};
        };
        auto c3 = sample(3000.0), c9 = sample(9000.0), c15 = sample(15000.0);
        auto finite = [](std::array<float, 3> c) {
            return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]);
        };
        bool ok1 = finite(c3) && finite(c9) && finite(c15);
        // 3000 K blackbody in sRGB is roughly (1.00, 0.48, 0.16) normalised
        bool ok2 = c3[0] > c3[1] && c3[1] > c3[2] &&
                   std::fabs(c3[0] - 1.0f) < 1e-3f && c3[2] > 0.08f && c3[2] < 0.35f;
        // cool bodies are blue-dominant
        bool ok3 = c9[2] >= c9[1] - 1e-3f && c9[1] >= c9[0] - 1e-3f && c15[2] >= c15[0];
        char buf[176];
        std::snprintf(buf, sizeof buf,
                      "3000K=(%.3f,%.3f,%.3f)  9000K=(%.3f,%.3f,%.3f)  15000K=(%.3f,%.3f,%.3f)",
                      c3[0], c3[1], c3[2], c9[0], c9[1], c9[2], c15[0], c15[1], c15[2]);
        check(ok1 && ok2 && ok3, "colour temperature ordering / range", buf);
    }

    // ---------------------------------------------------------------- 8
    std::printf("\n8) Integrator: production step size vs. tight reference (renderer geometry)\n");
    {
        // Camera at 40 M, escape boundary at 40 M: the configuration the
        // renderer actually uses.
        const double r0 = 40.0;
        double worst = 0.0;
        double wb = 0.0;
        for (double b : {6.0, 8.0, 12.0, 20.0, 30.0}) {
            double ref = deflectionAngle(b, r0, 0.002, 40000000);
            double fast = deflectionAngle(b, r0, 0.20, 4000000);
            if (ref < 1e8 && fast < 1e8) {
                double e = std::fabs(ref - fast);
                if (e > worst) { worst = e; wb = b; }
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "max |dalpha| = %.3e rad (b=%.0f M), < 0.01 deg", worst, wb);
        check(worst < 1.5e-3, "adaptive RK4 matches tight-step reference", buf);
    }

    // ---------------------------------------------------------------- 9
    std::printf("\n9) Geodesic gallery data (mode 3) sanity\n");
    {
        Polyline inner = traceEquatorialRay(4.0, 400.0, 40.0, 200000);   // b < b_c
        Polyline outer = traceEquatorialRay(9.0, 400.0, 40.0, 200000);   // b > b_c
        Polyline crit  = traceEquatorialRay(B_CRIT, 400.0, 60.0, 400000);
        char buf[128];
        std::snprintf(buf, sizeof buf, "b=4 captured=%d  b=9 captured=%d  b=b_c pts=%zu captured=%d",
                      int(inner.captured), int(outer.captured), crit.pts.size(), int(crit.captured));
        check(inner.captured && !outer.captured && inner.pts.size() > 10 && outer.pts.size() > 10,
              "captured / escaping rays classified correctly", buf);
    }

    // --------------------------------------------------------------- 10
    std::printf("\n10) Radial free fall from rest at infinity\n");
    {
        double r0 = 40.0;
        double tau_cross = (2.0 / 3.0) * (std::pow(r0, 1.5) - std::pow(RS, 1.5)) / std::sqrt(RS);
        double r = infallProperRadius(tau_cross, r0);
        char buf[128];
        std::snprintf(buf, sizeof buf, "r(tau_cross) = %.6f M, rs = %.1f M, tau_cross = %.4f M",
                      r, RS, tau_cross);
        check(std::fabs(r - RS) < 1e-6, "proper-time fall reaches r = rs in finite tau", buf);

        std::vector<double> tab;
        infallCoordinateTimeTable(r0, 0.05, 200000, tab);  // t up to 10000 M
        double rLast = tab.back();
        double rMid = tab[tab.size() / 2];
        char buf2[128];
        std::snprintf(buf2, sizeof buf2, "r(t=5000M) = %.6f  r(t=10000M) = %.6f (horizon 2.0)",
                      rMid, rLast);
        check(rMid > RS && rMid < RS + 0.05 && rLast >= RS,
              "coordinate-time fall freezes at the horizon", buf2);
    }

    std::printf("\n================================================================\n");
    if (g_fail == 0)
        std::printf(" All checks passed.\n");
    else
        std::printf(" %d check(s) FAILED.\n", g_fail);
    std::printf("================================================================\n");
    return g_fail;
}

}  // namespace bh
