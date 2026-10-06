#include "physics.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>

static int g_fails = 0;

static void expect(const char* name, bool ok, const char* detail) {
    if (ok) {
        std::printf("  PASS  %s  %s\n", name, detail);
    } else {
        std::printf("  FAIL  %s  %s\n", name, detail);
        ++g_fails;
    }
}

static void test_photon_orbit() {
    // Unstable circular null orbit at r = 3M, b = 3 sqrt(3).
    bh::State s;
    s.x = 3.0;
    s.y = 0.0;
    s.z = 0.0;
    s.px = 0.0;
    s.py = 1.0;
    s.pz = 0.0;
    s.pt = 1.0 / std::sqrt(3.0);
    const double c0 = std::fabs(bh::null_constraint(s, bh::RS));
    double max_dr = 0.0;
    double lambda = 0.0;
    const double quarter = 0.5 * M_PI * 3.0; // coordinate arc length ~ λ here since |v|=1
    int steps = 0;
    while (lambda < quarter && steps < 20000) {
        bh::Deriv d;
        bh::geodesic_deriv(s, bh::RS, d);
        const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const double h = 0.002 / std::max(speed, 1e-8);
        if (!bh::rk4(s, h, bh::RS)) break;
        lambda += h * speed;
        const double r = bh::radius(s);
        max_dr = std::max(max_dr, std::fabs(r - 3.0));
        ++steps;
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "constraint0=%.2e  max|r-3M|=%.3e over quarter orbit (%d steps)",
                  c0, max_dr, steps);
    expect("photon sphere stays at 3M", c0 < 1e-9 && max_dr < 5e-3, buf);
}

static void test_bcrit() {
    // Bisect the capture boundary. Reference is 3 sqrt(3) M.
    auto captured = [](double b) {
        bh::State s = bh::equatorial_ray(600.0, b);
        // Fixed small coordinate steps, independent of the visualization tuner.
        bool dipped = false;
        for (int i = 0; i < 80000; ++i) {
            const double r = bh::radius(s);
            if (r < bh::RS + 0.08) return true;
            bh::Deriv d;
            if (!bh::geodesic_deriv(s, bh::RS, d)) return true;
            const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            double target = 0.035 * r;
            if (r < 10.0) target = 0.012 * r;
            const double guard = 0.25 * (r - bh::RS);
            target = std::min(target, guard);
            const double h = std::clamp(target / std::max(speed, 1e-8), 1e-4, 0.8);
            const double r_before = r;
            if (!bh::rk4(s, h, bh::RS)) return true;
            const double r1 = bh::radius(s);
            if (r1 < r_before) dipped = true;
            bh::Deriv d1;
            if (!bh::geodesic_deriv(s, bh::RS, d1)) return true;
            const double out = s.x * d1.x + s.y * d1.y + s.z * d1.z;
            if (dipped && r1 > 500.0 && out > 0.0) return false;
        }
        return true;
    };
    double lo = 5.0, hi = 5.4;
    for (int i = 0; i < 28; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (captured(mid)) lo = mid;
        else hi = mid;
    }
    const double b = 0.5 * (lo + hi);
    const double err = std::fabs(b - bh::B_CRIT) / bh::B_CRIT;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "b=%.6f  exact=%.6f  rel err=%.3e", b, bh::B_CRIT, err);
    expect("critical impact parameter", err < 2e-3, buf);
}

static void test_turning_point() {
    const double b = 10.0;
    const double r_exact = bh::turning_point(b);
    bh::State s = bh::equatorial_ray(800.0, b);
    double min_r = 1e9;
    for (int i = 0; i < 100000; ++i) {
        const double r = bh::radius(s);
        min_r = std::min(min_r, r);
        if (r < bh::RS + 0.08) break;
        bh::Deriv d;
        if (!bh::geodesic_deriv(s, bh::RS, d)) break;
        const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        double target = 0.02 * r;
        if (r < 20.0) target = 0.008 * r;
        const double h = std::clamp(target / std::max(speed, 1e-8), 1e-4, 0.5);
        if (!bh::rk4(s, h, bh::RS)) break;
        bh::Deriv d1;
        if (!bh::geodesic_deriv(s, bh::RS, d1)) break;
        const double out = s.x * d1.x + s.y * d1.y + s.z * d1.z;
        if (min_r < 50.0 && bh::radius(s) > 400.0 && out > 0.0) break;
    }
    const double err = std::fabs(min_r - r_exact) / r_exact;
    char buf[180];
    std::snprintf(buf, sizeof(buf), "periapsis=%.5f  root=%.5f  rel err=%.3e", min_r, r_exact, err);
    expect("periapsis of b=10 ray", err < 2e-3 && min_r > 3.0, buf);
}

static void test_deflection() {
    const double b = 40.0;
    bh::State s = bh::equatorial_ray(3000.0, b);
    bh::Deriv d0;
    bh::geodesic_deriv(s, bh::RS, d0);
    const double sp0 = std::sqrt(d0.x * d0.x + d0.y * d0.y + d0.z * d0.z);
    const double vx0 = d0.x / sp0, vy0 = d0.y / sp0;
    double min_r = 1e9;
    double vx1 = vx0, vy1 = vy0;
    double max_c = 0.0;
    bool dipped = false;
    for (int i = 0; i < 200000; ++i) {
        const double r = bh::radius(s);
        min_r = std::min(min_r, r);
        max_c = std::max(max_c, std::fabs(bh::null_constraint(s, bh::RS)));
        bh::Deriv d;
        if (!bh::geodesic_deriv(s, bh::RS, d)) break;
        const double speed = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const double target = (r < 30.0) ? 0.015 * r : 0.04 * r;
        const double h = std::clamp(target / std::max(speed, 1e-8), 1e-4, 1.0);
        const double rb = r;
        if (!bh::rk4(s, h, bh::RS)) break;
        if (bh::radius(s) < rb) dipped = true;
        bh::Deriv d1;
        if (!bh::geodesic_deriv(s, bh::RS, d1)) break;
        const double sp = std::sqrt(d1.x * d1.x + d1.y * d1.y + d1.z * d1.z);
        const double out = s.x * d1.x + s.y * d1.y + s.z * d1.z;
        if (dipped && bh::radius(s) > 2500.0 && out > 0.0) {
            vx1 = d1.x / sp;
            vy1 = d1.y / sp;
            break;
        }
    }
    const double cth = std::clamp(vx0 * vx1 + vy0 * vy1, -1.0, 1.0);
    const double delta = std::acos(cth);
    const double series = 4.0 / b + (15.0 * M_PI / 4.0) / (b * b);
    const double err = std::fabs(delta - series) / series;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "delta=%.5f rad  series=%.5f  rel err=%.3e  |constraint|=%.2e",
                  delta, series, err, max_c);
    expect("weak-field deflection b=40", err < 0.03 && max_c < 1e-6, buf);
}

static void test_flux() {
    const bh::FluxTable t = bh::build_flux_table();
    // The Newtonian limit 3Ṁ/(8π r^3) is only reached once √r ≫ L_ISCO.
    // Check the peak location on the disk table, and the asymptote at r = 2000M.
    const double asym = 3.0 / (8.0 * M_PI);
    const double r_far = 2000.0;
    double integ = 0.0, prev_r = bh::ISCO, prev_y = 0.0;
    const int fine = 80000;
    for (int i = 1; i <= fine; ++i) {
        const double r = bh::ISCO + (r_far - bh::ISCO) * (static_cast<double>(i) / fine);
        const double y = bh::flux_integrand(r);
        integ += 0.5 * (prev_y + y) * (r - prev_r);
        prev_r = r;
        prev_y = y;
    }
    const double eol2 = 1.0 - 3.0 / r_far;
    const double dOmega = 1.5 * std::pow(r_far, -2.5);
    const double F_far = (dOmega / eol2) * integ / (4.0 * M_PI * r_far);
    const double scaled = r_far * r_far * r_far * F_far;
    const double err = std::fabs(scaled - asym) / asym;
    char buf[240];
    std::snprintf(buf, sizeof(buf),
                  "peak r=%.3fM  F(ISCO)=%.2e  r^3 F(2000)=%.5f  limit=%.5f  rel err=%.3e",
                  t.r_peak, t.F[0], scaled, asym, err);
    const bool peak_ok = t.r_peak > 8.0 && t.r_peak < 12.5 && t.F[0] == 0.0 && t.Fmax > 0.0;
    expect("Novikov–Thorne flux", peak_ok && err < 0.1, buf);
}

static void test_redshift_identity() {
    // For a static emitter, g = sqrt(α_em / α_cam), using conserved q_t.
    const double r_cam = 40.0;
    const double r_em = 10.0;
    const double a_cam = 1.0 - bh::RS / r_cam;
    const double a_em = 1.0 - bh::RS / r_em;
    const double q_t = std::sqrt(a_cam); // ω_loc = 1
    const double omega_cam = 1.0;
    const double u_t = 1.0 / std::sqrt(a_em);
    const double omega_em = q_t * u_t; // static, no spatial velocity
    const double g = omega_cam / omega_em;
    const double g_exact = std::sqrt(a_em / a_cam);
    const double err = std::fabs(g - g_exact) / g_exact;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "g=%.6f exact=%.6f rel err=%.2e", g, g_exact, err);
    expect("gravitational redshift", err < 1e-12, buf);
}

int main() {
    std::printf("Schwarzschild tests  (G = c = 1, M = 1, r_s = 2)\n");
    test_photon_orbit();
    test_bcrit();
    test_turning_point();
    test_deflection();
    test_flux();
    test_redshift_identity();
    if (g_fails) {
        std::printf("%d test(s) failed\n", g_fails);
        return 1;
    }
    std::printf("all physics tests passed\n");
    return 0;
}
