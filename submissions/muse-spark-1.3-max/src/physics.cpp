// Scientific-accuracy self-tests (--selftest). Each check compares the
// integrator / disk model against a textbook GR result.
#include "physics.hpp"

#include <cmath>
#include <cstdio>

namespace bh {
namespace {

int failures = 0;

void check(const char* name, double got, double want, double tol_rel) {
    const double denom = std::fabs(want) > 1e-12 ? std::fabs(want) : 1.0;
    const double err = std::fabs(got - want) / denom;
    const bool ok = err <= tol_rel;
    if (!ok) ++failures;
    std::printf("[%s] %-34s got %.9g want %.9g (rel err %.2e, tol %.1e)\n",
                ok ? "PASS" : "FAIL", name, got, want, err, tol_rel);
}

}  // namespace

int run_self_tests() {
    failures = 0;
    std::puts("black-hole-cpp-musemax --selftest (geometric units M = 1)");

    // 1. Kerr ISCO: Bardeen formula anchor points.
    check("ISCO a=0", isco_kerr(0.0), 6.0, 1e-9);
    check("ISCO a=+0.5", isco_kerr(0.5), 4.233002530, 1e-6);
    check("ISCO a=-0.5", isco_kerr(-0.5), 7.554584914, 1e-6);
    check("ISCO a=+0.9", isco_kerr(0.9), 2.320883282, 1e-6);
    check("ISCO a=+0.998", isco_kerr(0.998), 1.236970669, 1e-5);
    // Sanity: monotonic shrinking for prograde spin.
    check("ISCO monotonic", isco_kerr(0.7) < isco_kerr(0.2) ? 1.0 : 0.0, 1.0, 0);

    // 2. Photon capture threshold: bisection on the coordinate offset must
    // recover b_crit*sqrt(1-rs/r0), the exact finite-distance prediction
    // (at infinity this is b_crit = 3*sqrt(3) M).
    {
        const double r0 = 40.0;
        double lo = 4.0, hi = 6.5;  // lo captured, hi escapes
        for (int i = 0; i < 25; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (trace_impact(mid, r0).captured)
                lo = mid;
            else
                hi = mid;
        }
        check("shadow b_crit = 3sqrt(3)", 0.5 * (lo + hi),
              B_CRIT * std::sqrt(lapse(r0)), 2e-3);
    }

    // 3. Weak-field light deflection alpha = 4M/(L/E) (Einstein 1915).
    {
        const double b = 100.0, r0 = 600.0, esc = r0 * 1.2;
        TraceResult tr = trace_impact(b, r0);
        const double X = std::sqrt(r0 * r0 - b * b);
        const double Xe = std::sqrt(esc * esc - b * b);
        // Straight-line swept angle between the actual endpoints; the GR
        // ray sweeps more by exactly the deflection angle. Residual
        // finite-distance terms are O(M/r0) ~ 0.2%, inside tolerance.
        const double straight = std::atan2(b, -X) - std::atan2(b, Xe);
        const double alpha = tr.total_phi - straight;
        const double loverE = b / std::sqrt(lapse(r0));
        check("captured=false (b=100)", tr.captured ? 0.0 : 1.0, 1.0, 0);
        check("deflection 4M/b", alpha, 4.0 * MASS / loverE, 2e-2);
    }

    // 4. Redshift factor reduces to pure gravitational redshift for a
    // static emitter (v = 0): g = sqrt((1-rs/r)/(1-rs/r_obs)).
    {
        const double r = 6.0, r_obs = 16.0;
        const double E = 1.0;
        const double g = g_factor(E, r, r_obs, 0.0, 0.0);
        const double want = std::sqrt(lapse(r) / lapse(r_obs));
        check("static gravitational redshift", g, want, 1e-12);
    }

    // 5. Face-on Keplerian emitter: transverse Doppler + gravity combine
    // to g = sqrt(1 - 3M/r) (photon leaves perpendicular to motion).
    {
        const double r = 6.0, r_obs = 60.0;
        const double v = orbital_speed(r);  // 0.5c at ISCO
        check("orbital speed at ISCO", v, 0.5, 1e-12);
        // Photon emitted along disk normal: k_phys · phihat = 0.
        const double g = g_factor(1.0, r, r_obs, v, 0.0);
        const double want = std::sqrt(1.0 - 3.0 * MASS / r) /
                            std::sqrt(lapse(r_obs)) * std::sqrt(lapse(r_obs));
        // want = sqrt(1-3M/r) corrected for finite observer distance:
        const double want2 =
            std::sqrt(1.0 - 3.0 * MASS / r) / std::sqrt(lapse(r_obs));
        (void)want;
        check("face-on disk redshift", g, want2, 1e-9);
    }

    // 6. Flamm paraboloid anchor: w(2rs) = 2rs, w(rs) = 0.
    check("Flamm w(2rs)", flamm_w(2.0 * RS), 2.0 * RS, 1e-12);
    check("Flamm w(rs)=0", flamm_w(RS), 0.0, 0);  // exact zero

    // 7. Disk flux profile peaks at (49/36) r_isco (dF/dr = 0).
    {
        const double ri = 6.0;
        const double rp = disk_flux_peak_r(ri);
        const double f0 = disk_flux(rp, ri);
        const bool is_max =
            disk_flux(rp * 0.99, ri) < f0 && disk_flux(rp * 1.01, ri) < f0;
        check("disk flux peak at 49/36 ISCO", is_max ? 1.0 : 0.0, 1.0, 0);
        check("disk flux zero at ISCO", disk_flux(ri, ri), 0.0, 0);
    }

    // 8. Kerr orbital frequency anchor: Schwarzschild Ω = sqrt(M/r^3).
    check("Omega_kerr(r=8,a=0)", omega_kerr(8.0, 0.0),
          std::sqrt(MASS / 512.0), 1e-12);

    // 9. Shadow angular size anchor: static observer at 16M.
    // sin α = (b_crit/16) sqrt(1 - 2/16) -> α ≈ 17.67°.
    check("shadow angle at 16M (deg)", shadow_angle(16.0) * 180.0 / PI,
          17.6718, 1e-3);

    // 10. Doppler sign: a photon co-moving with the emitter (forward
    // beaming) must be blueshifted (g > 1); counter-moving redshifted.
    // Exact: g = sqrt(1-rs/r) / (sqrt(1-rs/r_obs) γ (1 -/+ v)).
    {
        const double r = 6.0, r_obs = 16.0, v = 0.5, E = 1.0;
        const double k = E / std::sqrt(lapse(r));  // |k_phys|
        const double gamma = 1.0 / std::sqrt(1 - v * v);
        const double g_fwd = g_factor(E, r, r_obs, v, +k);
        const double g_bwd = g_factor(E, r, r_obs, v, -k);
        const double pre = std::sqrt(lapse(r) / lapse(r_obs)) / gamma;
        check("Doppler forward blueshift", g_fwd, pre / (1 - v), 1e-12);
        check("Doppler backward redshift", g_bwd, pre / (1 + v), 1e-12);
        check("forward brighter than backward", g_fwd > g_bwd ? 1.0 : 0.0,
              1.0, 0);
    }

    // 11. Radial photon falls straight in (L = 0).
    {
        TraceResult tr =
            trace_world(Vec3(20, 0, 0), Vec3(-1, 0, 0), false);
        check("radial infall captured", tr.captured ? 1.0 : 0.0, 1.0, 0);
    }

    if (failures == 0)
        std::puts("selftest: ALL TESTS PASSED");
    else
        std::printf("selftest: %d TEST(S) FAILED\n", failures);
    return failures;
}

}  // namespace bh
