// verify.cpp — numerical verification of the Kerr geodesic integrator and the
// disk physics against closed-form results. Runs the *same* header code the
// GPU executes (kerr_geodesic.h), in double and in float.
#include "kerr_geodesic.h"
#include "physics.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>

static int g_fail = 0;
static void check(const char *name, double got, double want, double tol) {
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_fail;
    std::printf("  %-52s got % .7f  want % .7f  |err| %.2e  %s\n", name, got, want, std::fabs(got - want), ok ? "PASS" : "FAIL");
}

template <typename R>
static KGParams<R> makeParams(R a, R eps, R epsAng, R rEsc, int diskOn, R rDiskOut) {
    KGParams<R> p;
    p.rh = (R)bh::horizon(a);
    p.rStop = p.rh * R(1.005);
    p.rDoom = (R)bh::photonOrbit(std::fabs((double)a)) * R(0.999);
    p.rEsc = rEsc;
    p.rIsco = (R)bh::isco(a);
    p.rDiskOut = rDiskOut;
    p.rGridOut = R(0);
    p.eps = eps; p.epsAng = epsAng;
    p.diskOn = diskOn; p.gridOn = 0; p.maxSteps = 200000;
    return p;
}

// Trace a ray from an equatorial camera at radius rc with local direction (nr, nth, nph).
template <typename R>
static KGHit<R> traceDir(R a, R rc, R thc, R nr, R nth, R nph, KGParams<R> p, KGConst<R> *cOut = nullptr) {
    KGState<R> y; KGConst<R> c;
    kg_camera_ray<R>(rc, thc, R(0), a, nr, nth, nph, y, c);
    if (cOut) *cOut = c;
    const R cx = rc * KG_SIN(thc), cz = rc * KG_COS(thc);
    return kg_trace<R>(y, c, p, cx, R(0), cz);
}

// Critical |impact parameter| for equatorial rays: bisect on n_φ (sign picks prograde/retrograde).
static double criticalB(double a, double sign, double eps) {
    const double rc = 2000.0;
    KGParams<double> p = makeParams<double>(a, eps, eps, 4000.0, 0, 0.0);
    double lo = 0.0, hi = 10.0 / rc;   // n_φ magnitude: b ≈ n_φ r_c
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double nph = -sign * mid;   // photon direction is -n: L has sign of -nph
        const double nr = -std::sqrt(1.0 - mid * mid);
        KGHit<double> h = traceDir<double>(a, rc, bh::PI / 2, nr, 0.0, nph, p);
        if (h.type == KG_HIT_HORIZON) lo = mid; else hi = mid;
    }
    KGConst<double> c;
    const double mid = 0.5 * (lo + hi);
    traceDir<double>(a, rc, bh::PI / 2, -std::sqrt(1 - mid * mid), 0.0, -sign * mid, p, &c);
    return std::fabs(c.L / c.E);
}

int main(int argc, char **argv) {
    double eps = 0.05, epsAng = 0.05;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--eps") && i + 1 < argc) eps = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--epsang") && i + 1 < argc) epsAng = std::atof(argv[++i]);
    }
    std::printf("Kerr geodesic verification  (eps=%.3g epsAng=%.3g)\n", eps, epsAng);

    std::printf("\n[1] Horizon / ISCO / photon-orbit closed forms\n");
    check("r_+ (a=0)", bh::horizon(0.0), 2.0, 1e-12);
    check("r_+ (a=0.9)", bh::horizon(0.9), 1.0 + std::sqrt(0.19), 1e-12);
    check("r_isco (a=0)", bh::isco(0.0), 6.0, 1e-9);
    // ISCO must satisfy r² − 6r + 8a√r − 3a² = 0 (Bardeen, Press & Teukolsky 1972, eq. 2.21)
    for (double a : {0.9, -0.9, 0.998, 0.5}) {
        const double r = bh::isco(a); char nm[80];
        std::snprintf(nm, sizeof nm, "ISCO polynomial residual (a=%.3f, r_isco=%.5f)", a, r);
        check(nm, r * r - 6 * r + 8 * a * std::sqrt(r) - 3 * a * a, 0.0, 1e-9);
    }
    check("r_ph (a=0)", bh::photonOrbit(0.0), 3.0, 1e-9);
    check("r_ph (a=1, prograde)", bh::photonOrbit(1.0), 1.0, 1e-9);
    check("r_ph (a=1, retrograde)", bh::photonOrbit(-1.0), 4.0, 1e-9);

    std::printf("\n[2] Schwarzschild shadow: critical impact parameter b_c = 3*sqrt(3)\n");
    check("b_c (a=0, eps)", criticalB(0.0, 1.0, eps), 3.0 * std::sqrt(3.0), 2e-3);
    check("b_c (a=0, eps/4)", criticalB(0.0, 1.0, eps / 4), 3.0 * std::sqrt(3.0), 5e-4);

    std::printf("\n[3] Kerr shadow edges vs Bardeen (1973) critical impact parameters\n");
    for (double a : {0.5, 0.9, 0.998}) {
        const double xiPro = std::fabs(bh::bardeenXi(bh::photonOrbit(a), a));
        const double xiRet = std::fabs(bh::bardeenXi(bh::photonOrbit(-a), a));
        char nm[80];
        std::snprintf(nm, sizeof nm, "b_c prograde (a=%.3f)", a);  check(nm, criticalB(a, 1.0, eps), xiPro, 3e-3);
        std::snprintf(nm, sizeof nm, "b_c retrograde (a=%.3f)", a); check(nm, criticalB(a, -1.0, eps), xiRet, 3e-3);
    }

    std::printf("\n[4] Kerr shadow boundary off the equator (Bardeen curve, a=0.9, 6 points)\n");
    {
        const double a = 0.9, rc = 2000.0;
        KGParams<double> p = makeParams<double>(a, eps, epsAng, 4000.0, 0, 0.0);
        const double rpro = bh::photonOrbit(a), rret = bh::photonOrbit(-a);
        int okCount = 0, total = 0;
        for (int i = 1; i <= 6; ++i) {
            const double rp = rpro + (rret - rpro) * i / 7.0;
            const double xi = bh::bardeenXi(rp, a), eta = bh::bardeenEta(rp, a);
            if (eta < 0) continue;
            for (double scale : {0.985, 1.015}) {
                // Build the ray with L/E = xi*scale and Q/E² = eta*scale² at θ = π/2.
                double al, om, Sg, Dl, AA; kg_zamo<double>(rc, bh::PI / 2, a, al, om, Sg, Dl, AA);
                const double xs = xi * scale, es = eta * scale * scale;
                const double L = xs * al / (1.0 - xs * om);
                const double E = al + om * L;
                const double pth = std::sqrt(es) * E;
                const double mph = L / std::sqrt(AA / Sg);
                const double mth = pth / std::sqrt(Sg);
                const double mr = std::sqrt(std::max(0.0, 1.0 - mph * mph - mth * mth));   // photon moves outward (came from the hole)
                KGHit<double> h = traceDir<double>(a, rc, bh::PI / 2, -mr, -mth, -mph, p);   // viewing direction n = -m
                const bool captured = h.type == KG_HIT_HORIZON;
                const bool want = scale < 1.0;
                ++total; if (captured == want) ++okCount;
                std::printf("  r_p=%.4f xi=% .4f eta=%.4f scale=%.3f -> %-8s %s\n", rp, xi, eta, scale,
                            captured ? "captured" : "escaped", captured == want ? "PASS" : "FAIL");
                if (captured != want) ++g_fail;
            }
        }
        std::printf("  %d/%d boundary probes correct\n", okCount, total);
    }

    std::printf("\n[5] Weak-field deflection vs 4/b + 15pi/(4b^2) + 128/(3b^3)\n");
    for (double b : {50.0, 100.0, 200.0}) {
        const double rc = 20000.0;
        KGParams<double> p = makeParams<double>(0.0, eps, epsAng, 20000.0, 0, 0.0);
        double al, om, Sg, Dl, AA; kg_zamo<double>(rc, bh::PI / 2, 0.0, al, om, Sg, Dl, AA);
        const double nph = -b * al / std::sqrt(AA / Sg);
        const double nr = -std::sqrt(1.0 - nph * nph);
        KGConst<double> c;
        KGHit<double> h = traceDir<double>(0.0, rc, bh::PI / 2, nr, 0.0, nph, p, &c);
        // viewing direction in Cartesian for camera at +x: r̂=x, θ̂=-z, φ̂=y
        const double nx = nr, ny = nph, nz = 0.0;
        const double dot = nx * h.dx + ny * h.dy + nz * h.dz;
        const double defl = std::acos(std::min(1.0, std::max(-1.0, dot)));
        const double bb = std::fabs(c.L / c.E);
        const double want = 4.0 / bb + 15.0 * bh::PI / (4.0 * bb * bb) + 128.0 / (3.0 * bb * bb * bb);
        char nm[80]; std::snprintf(nm, sizeof nm, "deflection (b=%.1f, %d steps)", bb, h.steps);
        check(nm, defl, want, 2e-5 + 1e-3 * want);
    }

    std::printf("\n[6] Null constraint H = 0 conservation along a near-critical ray (a=0.9)\n");
    {
        const double a = 0.9, rc = 30.0;
        KGParams<double> p = makeParams<double>(a, eps, epsAng, 100.0, 0, 0.0);
        KGState<double> y; KGConst<double> c;
        const double b = 2.86, nph = -b / rc;   // just outside prograde critical ~2.85 (loops around)
        kg_camera_ray<double>(rc, bh::PI / 2, 0.0, a, -std::sqrt(1 - nph * nph), 0.0, nph, y, c);
        double Hmax = 0.0; int steps = 0; double rmin = 1e9;
        for (int i = 0; i < 100000; ++i) {
            KGDeriv<double> D = kg_deriv(y, c);
            const double s = std::sin(y.th); const double P = (y.r * y.r + a * a) * c.E - a * c.L;
            const double Dl = y.r * y.r - 2 * y.r + a * a;
            const double terms = std::fabs(Dl * y.pr * y.pr) + y.pth * y.pth + P * P / Dl + (c.L * c.L) / (s * s);
            Hmax = std::max(Hmax, std::fabs(2 * (y.r * y.r + a * a * std::cos(y.th) * std::cos(y.th)) * D.H) / terms);
            const double h = -kg_step_size(y, D.d, c, p);
            y = kg_rk4(y, D.d, c, h); ++steps; rmin = std::min(rmin, y.r);
            if (y.r > 100.0 || y.r < p.rStop) break;
        }
        std::printf("  steps=%d  r_min=%.4f  max relative |2ΣH| / Σ|terms| = %.3e\n", steps, rmin, Hmax);
        check("relative null-constraint drift < 1e-6", Hmax < 1e-6 ? 0.0 : Hmax, 0.0, 1e-6);
    }

    std::printf("\n[7] Disk redshift, face-on Schwarzschild: g*E = sqrt(1 - 3/r)\n");
    {
        const double rc = 2000.0, thc = 1e-3;
        KGParams<double> p = makeParams<double>(0.0, eps, epsAng, 4000.0, 1, 40.0);
        for (double b : {7.0, 10.0, 20.0}) {
            const double nth = b / rc; const double nr = -std::sqrt(1 - nth * nth);
            KGConst<double> c;
            KGHit<double> h = traceDir<double>(0.0, rc, thc, nr, nth, 0.0, p, &c);
            char nm[80]; std::snprintf(nm, sizeof nm, "g*E at r_hit=%.4f (type %d)", h.r, h.type);
            check(nm, h.g * c.E, std::sqrt(1 - 3.0 / h.r), 2e-5);
        }
    }

    std::printf("\n[8] Page–Thorne flux sanity\n");
    check("F(r_isco) (a=0.9)", bh::pageThorneFlux(bh::isco(0.9) * (1 + 1e-12), 0.9), 0.0, 1e-9);
    check("F/F_Newton at r=1e5 (a=0.9)", bh::pageThorneFlux(1e5, 0.9) / (3.0 / (8 * bh::PI * 1e15)), 1.0, 2e-2);
    check("F/F_Newton at r=1e5 (a=0)", bh::pageThorneFlux(1e5, 0.0) / (3.0 / (8 * bh::PI * 1e15)), 1.0, 2e-2);
    {
        // a=0 profile must match the Shakura–Sunyaev relativistic-free form only asymptotically; check monotone peak location ~ (49/36)*6
        auto lut = bh::diskTemperatureProfile(0.0, 6.0, 60.0, 4096);
        int imax = 0; for (int i = 0; i < 4096; ++i) if (lut[i] > lut[imax]) imax = i;
        const double rpeak = 6.0 + 54.0 * (imax + 0.5) / 4096;
        std::printf("  T(r) peaks at r = %.3f (Newtonian zero-torque disk peaks at 49/36 r_in = %.3f; GR version differs)\n", rpeak, 49.0 / 36.0 * 6.0);
        check("radiative efficiency (a=0) = 1 - sqrt(8/9)", bh::efficiency(0.0), 1.0 - std::sqrt(8.0 / 9.0), 1e-9);
        check("radiative efficiency (a=0.998)", bh::efficiency(0.998), 0.3210, 5e-4);
    }

    std::printf("\n[9] Embedding diagram vs Flamm paraboloid (a=0): Z(r) - Z(rOut) = 2sqrt(2(r-2)) - 2sqrt(2(rOut-2))\n");
    {
        auto emb = bh::embedding(0.0, 40.0, 200);
        double maxErr = 0.0;
        for (auto &s : emb) {
            const double want = 2 * std::sqrt(2 * (s.r - 2)) - 2 * std::sqrt(2 * 38.0);
            maxErr = std::max(maxErr, std::fabs(s.Z - want));
        }
        check("max |Z error| over 200 samples", maxErr, 0.0, 2e-3);
        auto embK = bh::embedding(0.9, 40.0, 200);
        std::printf("  a=0.9: embedding exists down to r = %.4f (r_+ = %.4f), throat depth %.3f\n", embK.front().r, bh::horizon(0.9), embK.front().Z);
    }

    std::printf("\n[10] float32 (GPU path) vs double, camera r=25 θ=80° a=0.9, 64x64 rays, disk 2.32..25\n");
    {
        const double a = 0.9, rc = 25.0, thc = 80.0 * bh::PI / 180.0;
        KGParams<double> pd = makeParams<double>(a, eps, epsAng, 150.0, 1, 25.0);
        KGParams<float> pf = makeParams<float>((float)a, (float)eps, (float)epsAng, 150.0f, 1, 25.0f);
        int agree = 0, total = 0; double maxR = 0, maxG = 0, maxDir = 0; long stepsF = 0, maxSteps = 0;
        auto t0 = std::chrono::steady_clock::now();
        const double tanH = std::tan(0.5 * 40.0 * bh::PI / 180.0);
        for (int j = 0; j < 64; ++j) for (int i = 0; i < 64; ++i) {
            const double x = (i + 0.5) / 64 * 2 - 1, yv = (j + 0.5) / 64 * 2 - 1;
            double nr = -1, nth = -yv * tanH, nph = x * tanH;
            const double inv = 1 / std::sqrt(nr * nr + nth * nth + nph * nph); nr *= inv; nth *= inv; nph *= inv;
            KGHit<double> hd = traceDir<double>(a, rc, thc, nr, nth, nph, pd);
            KGHit<float> hf = traceDir<float>((float)a, (float)rc, (float)thc, (float)nr, (float)nth, (float)nph, pf);
            ++total; stepsF += hf.steps; maxSteps = std::max<long>(maxSteps, hf.steps);
            if (hd.type == hf.type) {
                ++agree;
                if (hd.type == KG_HIT_DISK) { maxR = std::max(maxR, std::fabs(hd.r - hf.r)); maxG = std::max(maxG, std::fabs(hd.g - hf.g)); }
                if (hd.type == KG_HIT_SKY) maxDir = std::max(maxDir, std::acos(std::min(1.0, hd.dx * hf.dx + hd.dy * hf.dy + hd.dz * hf.dz)));
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        std::printf("  hit-type agreement %d/%d, max |Δr_disk| %.2e, max |Δg| %.2e, max sky-direction error %.2e rad\n", agree, total, maxR, maxG, maxDir);
        std::printf("  float path: mean %.1f steps/ray, max %ld steps; CPU time %.2f ms for %d rays (both precisions)\n",
                    (double)stepsF / total, maxSteps, std::chrono::duration<double, std::milli>(t1 - t0).count(), total);
        check("hit-type agreement fraction", (double)agree / total, 1.0, 0.01);
        check("max disk radius mismatch", maxR, 0.0, 5e-3);
        check("max redshift mismatch", maxG, 0.0, 2e-3);
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
