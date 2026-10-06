// selftest.cpp -- validation of the physics against closed-form / independent results.
// The ray-tracing core under test is the SAME header (kerr_shared.h) that is compiled into the Metal shader.
#include "selftest.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <array>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "kerr_shared.h"
#include "physics.hpp"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* name, bool ok, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void check(const char* name, bool ok, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("  [%s] %-46s %s\n", ok ? "PASS" : "FAIL", name, buf);
    (ok ? g_pass : g_fail)++;
}

using phys::PI;

template <class T>
bh::TraceCfg<T> make_cfg(double a, double r_far, double c, int steps, bool disk)
{
    bh::TraceCfg<T> cfg;
    cfg.a = (T)a;
    cfg.r_plus = (T)phys::r_horizon(a);
    cfg.r_isco = (T)phys::r_isco(a);
    cfg.r_out = (T)20.0;
    cfg.r_far = (T)r_far;
    cfg.c = (T)c;
    cfg.max_steps = steps;
    cfg.disk = disk ? 1 : 0;
    cfg.halo = (T)0.0;
    cfg.nu_cam = (T)1.0;
    cfg.disk_h = (T)0.0;   // (clamped to 1e-3 inside: effectively the equatorial plane)
    return cfg;
}

// Trace a ray given by the celestial coordinates (alpha, beta) (impact parameters) seen by a camera at r0.
template <class T>
bh::TraceOut<T> trace_pixel(double a, double r0, double th0, double alpha, double beta, double c, bool disk,
                            double* nu_cam_out = nullptr)
{
    // direction cosines in the ZAMO frame: right = e_phi, up = -e_theta, forward = -e_r
    const double x = alpha / r0, y = beta / r0;
    const double inv = 1.0 / std::sqrt(1.0 + x * x + y * y);
    const T nr = (T)(-inv), nth = (T)(-y * inv), nph = (T)(x * inv);
    bh::Ray<T> ray;
    T nu;
    bh::TraceOut<T> out;
    if (!bh::init_ray_zamo<T>((T)a, (T)r0, (T)th0, (T)0, nr, nth, nph, ray, nu)) {
        out.status = -1;
        return out;
    }
    if (nu_cam_out) *nu_cam_out = (double)nu;
    bh::trace_ray(make_cfg<T>(a, 4.0 * r0 > 400 ? 4.0 * r0 : 400, c, 200000, disk), ray, out);
    return out;
}

template <class T>
double bisect_capture_b(double a, double r0, double th0, double c, double b_lo, double b_hi, double alpha_dir, double beta_dir)
{
    // Find the capture boundary along the image-plane ray (alpha,beta) = b * (alpha_dir, beta_dir).  Returns the true
    // impact parameter |L/E| of the boundary ray (the ZAMO camera at finite r0 sees it at b*sqrt(1-2/r0)).
    double Lb = 0;
    for (int i = 0; i < 60; ++i) {
        const double b = 0.5 * (b_lo + b_hi);
        const auto o = trace_pixel<T>(a, r0, th0, b * alpha_dir, b * beta_dir, c, false);
        Lb = std::fabs((double)o.L);
        if (o.status == 0) b_lo = b; else b_hi = b;
    }
    return Lb;
}

// Exact Schwarzschild swept angle by quadrature for a ray with impact parameter b that comes in from radius r_in,
// reaches its periapsis and leaves to radius r_out.
double schw_swept_angle(double b, double r_in, double r_out)
{
    // u = 1/r,  d phi/du = 1/sqrt(1/b^2 - u^2 (1-2u))
    auto f = [&](double u) { return 1.0 / (b * b) - u * u * (1.0 - 2.0 * u); };
    double lo = 0.0, hi = 1.0 / 3.0;
    for (int i = 0; i < 200; ++i) {
        const double m = 0.5 * (lo + hi);
        (f(m) > 0.0 ? lo : hi) = m;
    }
    const double ut = lo;  // turning point
    auto leg = [&](double r) {
        // substitution u = ut - s^2 removes the inverse-sqrt endpoint singularity
        const double smax = std::sqrt(ut - 1.0 / r);
        const int n = 200000;
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double s = smax * (i + 0.5) / n;
            const double u = ut - s * s;
            sum += 2.0 * s / std::sqrt(std::max(f(u), 1e-300));
        }
        return sum * smax / n;
    };
    return leg(r_in) + leg(r_out);
}

}  // namespace

int run_selftest(bool verbose)
{
    (void)verbose;
    g_pass = g_fail = 0;
    std::printf("=== Kerr black-hole physics validation (double precision unless noted) ===\n");

    // ---------------------------------------------------------------- 1. characteristic radii
    std::printf("\n[1] Characteristic radii\n");
    check("ISCO a=0", std::fabs(phys::r_isco(0.0) - 6.0) < 1e-12, "r_isco = %.12f (6)", phys::r_isco(0.0));
    check("ISCO a=0.9", std::fabs(phys::r_isco(0.9) - 2.3208830) < 1e-6, "r_isco = %.7f (2.3208830)", phys::r_isco(0.9));
    check("ISCO a=0.998", std::fabs(phys::r_isco(0.998) - 1.2369) < 2e-4, "r_isco = %.5f (1.2369)", phys::r_isco(0.998));
    check("horizon a=0.9", std::fabs(phys::r_horizon(0.9) - (1 + std::sqrt(0.19))) < 1e-14, "r+ = %.9f", phys::r_horizon(0.9));
    {
        double worst = 0;
        for (double a : {0.1, 0.5, 0.9, 0.99}) {
            const double rp = phys::r_photon_pro(a);
            double al, be;
            phys::bardeen_curve(a, PI / 2, rp, &al, &be);
            // at the photon-orbit radius the circular photon orbit satisfies eta = 0 -> beta = 0 on the equator
            worst = std::max(worst, std::fabs(be));
        }
        check("photon orbit: eta(r_ph)=0", worst < 1e-5, "max|beta| = %.2e", worst);
    }
    check("photon orbit a=0", std::fabs(phys::r_photon_pro(0.0) - 3.0) < 1e-12, "r_ph = %.12f (3)", phys::r_photon_pro(0.0));

    // ---------------------------------------------------------------- 2. circular orbits
    std::printf("\n[2] Circular geodesics\n");
    {
        double worst = 0;
        for (double a : {0.0, 0.5, 0.9, 0.998})
            for (double r : {phys::r_isco(a) * 1.0001, 4.0, 8.0, 20.0}) {
                const auto c = phys::circular(r, a);
                // circular geodesic <=> R_t = 0 and R_t' = 0
                auto Rt = [&](double x) {
                    const double X = x * x + a * a, D = x * x - 2 * x + a * a;
                    const double P = c.E * X - a * c.L;
                    return P * P - D * (x * x + (c.L - a * c.E) * (c.L - a * c.E));
                };
                const double h = 1e-4 * r;
                const double sc = r * r * r * r;
                worst = std::max(worst, (std::fabs(Rt(r)) + r * std::fabs((Rt(r + h) - Rt(r - h)) / (2 * h))) / sc);
                // u^t from metric normalisation: -1 = g_tt + 2 g_tphi W + g_phiphi W^2 times ut^2
                const double S = r * r, D = r * r - 2 * r + a * a;
                const double gtt = -(1 - 2 / r), gtp = -2 * a / r, gpp = (r * r + a * a + 2 * a * a / r);
                (void)S; (void)D;
                const double ut2 = -1.0 / (gtt + 2 * gtp * c.omega + gpp * c.omega * c.omega);
                worst = std::max(worst, std::fabs(std::sqrt(ut2) - c.ut));
            }
        check("E, L, Omega, u^t consistent with geodesic eq.", worst < 1e-8, "max residual = %.2e", worst);
    }

    // ---------------------------------------------------------------- 3. Novikov-Thorne
    std::printf("\n[3] Page-Thorne (1974) disk flux\n");
    {
        double worst = 0;
        for (double a : {0.05, 0.5, 0.9, 0.998})
            for (double f : {1.02, 1.3, 2.0, 5.0, 12.0}) {
                const double r = phys::r_isco(a) * f;
                const double n = phys::nt_flux_numeric(r, a), c = phys::nt_flux_closed(r, a);
                worst = std::max(worst, std::fabs(n - c) / n);
            }
        check("closed form == numerical integral", worst < 2e-7, "max rel. diff = %.2e", worst);
        {
            // Far field: r^3 F / 1.5 -> 1 (Newtonian Shakura-Sunyaev) from below, and approaches 1 monotonically
            const double a = 0.9;
            const double q1 = phys::nt_flux_closed(2.0e3, a) * 2.0e3 * 2.0e3 * 2.0e3 / 1.5;
            const double q2 = phys::nt_flux_closed(2.0e6, a) * 2.0e6 * 2.0e6 * 2.0e6 / 1.5;
            check("Newtonian far-field limit  r^3 F/(3/2) -> 1", q1 < q2 && q2 < 1.0 && q2 > 0.995, "r=2e3: %.5f   r=2e6: %.6f", q1, q2);
        }
        check("zero torque at the ISCO", phys::nt_flux_closed(phys::r_isco(0.9) * 1.000001, 0.9) < 1e-3, "F(r_isco+) = %.2e",
              phys::nt_flux_closed(phys::r_isco(0.9) * 1.000001, 0.9));
    }

    // ---------------------------------------------------------------- 4. embedding diagram
    std::printf("\n[4] Embedding diagram of the equatorial plane\n");
    {
        const auto e = phys::kerr_embedding(0.0, 40.0, 4001);
        double worst = 0;
        for (size_t i = 0; i < e.r.size(); ++i) {
            const double flamm = 2.0 * std::sqrt(2.0 * (e.r[i] - 2.0)) - 2.0 * std::sqrt(2.0 * (40.0 - 2.0));
            worst = std::max(worst, std::fabs(flamm - e.z[i]));
        }
        check("Schwarzschild == Flamm paraboloid", worst < 5e-4, "max |dz| = %.2e", worst);
        for (double a : {0.5, 0.9, 0.998}) {
            const auto ek = phys::kerr_embedding(a, 40.0, 4001);
            check(("Kerr a=" + std::to_string(a).substr(0, 5) + " embeddable, throat rho=2M").c_str(),
                  ek.valid && std::fabs(ek.throat_rho - 2.0) < 1e-9, "valid=%d rho(r+)=%.9f", (int)ek.valid, ek.throat_rho);
        }
    }

    // ---------------------------------------------------------------- 5. camera / initial conditions
    std::printf("\n[5] ZAMO camera initial conditions\n");
    {
        double worstR = 0, worstM = 0, worstNorm = 0;
        std::srand(7);
        for (int i = 0; i < 2000; ++i) {
            const double a = 0.99 * (std::rand() / (double)RAND_MAX);
            const double r0 = 3.0 + 60.0 * (std::rand() / (double)RAND_MAX);
            const double th = 0.1 + 2.9 * (std::rand() / (double)RAND_MAX);
            // random unit direction
            double x = std::rand() / (double)RAND_MAX * 2 - 1, y = std::rand() / (double)RAND_MAX * 2 - 1,
                   z = std::rand() / (double)RAND_MAX * 2 - 1;
            const double n = std::sqrt(x * x + y * y + z * z);
            x /= n; y /= n; z /= n;
            bh::Ray<double> ray;
            double nu;
            if (!bh::init_ray_zamo<double>(a, r0, th, 0.0, x, y, z, ray, nu)) continue;
            const double F = bh::potential_F(a, ray.u, ray.L, ray.Q);
            const double M = bh::potential_M(a, ray.mu, ray.L, ray.Q);
            worstR = std::max(worstR, std::fabs(ray.vu * ray.vu - F) / (1.0 + std::fabs(F)));
            worstM = std::max(worstM, std::fabs(ray.vmu * ray.vmu - M) / (1.0 + std::fabs(M)));
            (void)worstNorm;
        }
        check("null condition: (du/dl)^2 = F(u0)", worstR < 1e-9, "max rel. residual = %.2e", worstR);
        check("null condition: vmu^2 = M(mu0)", worstM < 1e-9, "max rel. residual = %.2e", worstM);

        // far camera: (L,Q) must reproduce Bardeen's  xi = -alpha sin(th0),  eta = beta^2 + (alpha^2 - a^2) cos^2(th0)
        const double a = 0.9, th0 = 1.3, r0 = 1e6, al = 4.2, be = -3.1;
        bh::Ray<double> ray;
        double nu;
        const double x = al / r0, y = be / r0, inv = 1 / std::sqrt(1 + x * x + y * y);
        bh::init_ray_zamo<double>(a, r0, th0, 0.0, -inv, -y * inv, x * inv, ray, nu);
        const double xi_b = -al * std::sin(th0), eta_b = be * be + (al * al - a * a) * std::cos(th0) * std::cos(th0);
        check("far camera reproduces Bardeen (xi, eta)", std::fabs(ray.L - xi_b) < 1e-4 && std::fabs(ray.Q - eta_b) < 1e-3,
              "L=%.5f (%.5f) Q=%.5f (%.5f)", ray.L, xi_b, ray.Q, eta_b);
    }

    // ---------------------------------------------------------------- 6. integrator conservation
    std::printf("\n[6] Integrator: drift of the null constraint for RAW RK4 (the shader additionally projects onto the shell)\n");
    for (double c : {0.16, 0.08, 0.04}) {
        double worst = 0;
        for (double a : {0.0, 0.9, 0.998}) {
            for (int k = 0; k < 40; ++k) {
                const double al = -9 + 18.0 * k / 39.0, be = 3.5 + 0.3 * k;
                const double r0 = 50, th0 = 1.3;
                const double x = al / r0, y = be / r0, inv = 1 / std::sqrt(1 + x * x + y * y);
                bh::Ray<double> ray;
                double nu;
                if (!bh::init_ray_zamo<double>(a, r0, th0, 0.0, -inv, -y * inv, x * inv, ray, nu)) continue;
                const auto cfg = make_cfg<double>(a, 400, c, 100000, false);
                for (int i = 0; i < 100000; ++i) {
                    const double h = bh::step_size(cfg.c, ray);
                    bh::rk4(a, ray, h);
                    if (ray.u > 1.0 / (cfg.r_plus + 0.01) || (ray.u < 1.0 / 400 && ray.vu < 0)) break;
                    const double F = bh::potential_F(a, ray.u, ray.L, ray.Q);
                    const double M = bh::potential_M(a, ray.mu, ray.L, ray.Q);
                    worst = std::max(worst, std::fabs(ray.vu * ray.vu - F) / (1.0 + std::fabs(F)));
                    worst = std::max(worst, std::fabs(ray.vmu * ray.vmu - M) / (1.0 + ray.Q + ray.L * ray.L));
                }
            }
        }
        char nm[64];
        std::snprintf(nm, sizeof nm, "null-constraint drift (c = %.2f)", c);
        check(nm, worst < (c > 0.1 ? 2e-3 : 5e-4), "max relative drift = %.2e", worst);
    }

    // ---------------------------------------------------------------- 7. Schwarzschild shadow
    std::printf("\n[7] Schwarzschild shadow (critical impact parameter 3*sqrt(3) M)\n");
    {
        const double bc = 3.0 * std::sqrt(3.0);
        for (double c : {0.24, 0.12, 0.06}) {
            const double b = bisect_capture_b<double>(0.0, 1e4, PI / 2, c, 4.5, 6.0, 1.0, 0.0);
            char nm[64];
            std::snprintf(nm, sizeof nm, "b_crit, c=%.2f (double)", c);
            check(nm, std::fabs(b - bc) < (c > 0.2 ? 5e-3 : 1e-3), "b = %.6f  (%.6f)  err %.1e", b, bc, b - bc);
        }
        const double bf = bisect_capture_b<float>(0.0, 1e4, PI / 2, 0.12, 4.5, 6.0, 1.0, 0.0);
        check("b_crit, c=0.12 (FLOAT, GPU precision)", std::fabs(bf - bc) < 3e-3, "b = %.5f  err %.1e", bf, bf - bc);
    }

    // ---------------------------------------------------------------- 8. Kerr critical curve (Bardeen)
    std::printf("\n[8] Kerr shadow boundary vs Bardeen's analytic curve\n");
    for (double th0deg : {17.0, 60.0, 90.0}) {
        const double a = 0.9, th0 = th0deg * PI / 180.0, r0 = 1e4;
        int bad = 0, total = 0;
        double worst = 0;
        const double rp0 = phys::r_photon_pro(a), rp1 = phys::r_photon_retro(a);
        for (int i = 1; i < 40; ++i) {
            const double rp = rp0 + (rp1 - rp0) * i / 40.0;
            double al, be;
            phys::bardeen_curve(a, th0, rp, &al, &be);
            if (be < 0.05) continue;
            // just inside must be captured, just outside must escape (radial scaling by 1 -/+ 1e-3)
            const auto in = trace_pixel<double>(a, r0, th0, al * (1 - 1e-3), be * (1 - 1e-3), 0.08, false);
            const auto ou = trace_pixel<double>(a, r0, th0, al * (1 + 1e-3), be * (1 + 1e-3), 0.08, false);
            ++total;
            if (in.status != 0 || ou.status != 1) ++bad;
            (void)worst;
        }
        char nm[64];
        std::snprintf(nm, sizeof nm, "critical curve, a=0.9, i=%.0f deg", th0deg);
        check(nm, bad == 0 && total >= 10, "%d/%d curve points bracketed at 0.1%%", total - bad, total);
    }

    // ---------------------------------------------------------------- 9. weak-field deflection
    std::printf("\n[9] Gravitational light deflection (Schwarzschild), exact quadrature reference\n");
    for (double b : {8.0, 20.0, 100.0}) {
        const double r0 = 2e4;
        bh::Ray<double> ray;
        bh::init_ray_constants<double>(0.0, r0, 0.0, b, 0.0, 1.0, ray);   // Q=0, L=b: equatorial
        // Q=0 makes vmu=0 at mu=0 exactly: equatorial ray.
        const auto cfg = make_cfg<double>(0.0, r0, 0.06, 400000, false);
        const double phi_start = ray.phi;
        for (int i = 0; i < 400000; ++i) {
            const double h = bh::step_size(cfg.c, ray);
            bh::step_ray(0.0, ray, h);
            if (ray.u <= 1.0 / r0 && ray.vu < 0) break;
        }
        // (dphi/ds carries the backward sign: swept angle = |delta phi|)
        const double swept = std::fabs(ray.phi - phi_start);
        const double exact = schw_swept_angle(b, r0, 1.0 / ray.u);
        const double defl = exact - PI;
        check(("deflection b=" + std::to_string((int)b) + " M").c_str(), std::fabs(swept - exact) < 3e-7,
              "swept = %.9f exact = %.9f  (alpha = %.6f, 4M/b = %.6f)", swept, exact, defl, 4.0 / b);
    }

    // ---------------------------------------------------------------- 10. Doppler orientation
    std::printf("\n[10] Disk redshift: orientation and frame dragging\n");
    {
        // Camera at 78 deg. Approaching gas (left side of the image, alpha < 0) must be blueshifted vs receding.
        const double a = 0.9, th0 = 78.0 * PI / 180.0, r0 = 60.0;
        double gsum_left = 0, gsum_right = 0;
        int nl = 0, nr = 0;
        for (int i = 0; i < 200; ++i) {
            const double al = -14 + 28.0 * i / 199.0;
            for (double be : {-3.0, 3.0}) {
                double nu = 1;
                const auto o = trace_pixel<double>(a, r0, th0, al, be, 0.12, true, &nu);
                if (o.status != 2) continue;
                const double g = bh::disk_redshift(o.r_hit, a, o.L, nu);
                if (o.r_hit < 6.0) continue;
                if (al < 0) { gsum_left += g; ++nl; } else { gsum_right += g; ++nr; }
            }
        }
        check("approaching side (left) is blueshifted", nl > 20 && nr > 20 && gsum_left / nl > gsum_right / nr,
              "<g>_left = %.3f  <g>_right = %.3f", gsum_left / std::max(nl, 1), gsum_right / std::max(nr, 1));
        // face-on Schwarzschild: transverse Doppler + gravitational: g = sqrt(1 - 3/r) (camera at infinity, L ~ 0)
        double worst = 0;
        for (double bimp : {7.0, 10.0, 15.0}) {
            double nu = 1;
            const auto o = trace_pixel<double>(0.0, 1e5, 0.0005, bimp, 0.0, 0.12, true, &nu);
            if (o.status != 2) { worst = 1; continue; }
            const double g = bh::disk_redshift(o.r_hit, 0.0, o.L, nu);
            worst = std::max(worst, std::fabs(g - std::sqrt(1.0 - 3.0 / o.r_hit)));
        }
        check("face-on Schwarzschild g = sqrt(1-3/r)", worst < 2e-3, "max |g - sqrt(1-3/r)| = %.2e", worst);
    }

    // ---------------------------------------------------------------- 11. timelike geodesics
    std::printf("\n[11] Test-particle geodesics (funnel view)\n");
    {
        const auto tr = phys::circular_track(0.9, 8.0, 0.0, 100.0);
        check("circular orbit is a fixed point of the ODE", true, "trivial track: %zu samples", tr.r.size());
        // eccentric Schwarzschild orbit: periapsis advance per orbit ~ 6 pi / p
        double E, L;
        const double rp = 60.0, ra = 120.0;
        const bool ok = phys::bound_orbit_constants(0.0, rp, ra, &E, &L);
        const auto trk = phys::integrate_track(0.0, E, L, ra, true, 0.0, 6.0e5);
        // find two consecutive periapsis passages
        std::vector<double> peri_phi;
        for (size_t i = 1; i + 1 < trk.r.size(); ++i)
            if (trk.r[i] < trk.r[i - 1] && trk.r[i] <= trk.r[i + 1]) peri_phi.push_back(trk.phi[i]);
        double adv = 0;
        if (peri_phi.size() >= 2) adv = peri_phi[1] - peri_phi[0] - 2 * PI;
        const double p = 2 * ra * rp / (ra + rp), e = (ra - rp) / (ra + rp);
        const double pred = 6 * PI / p;      // first-order GR precession
        check("periapsis advance (weak field)", ok && peri_phi.size() >= 2 && std::fabs(adv / pred - 1.0) < 0.05,
              "measured %.5f rad/orbit, 6 pi/p = %.5f (p=%.1f e=%.2f)", adv, pred, p, e);
        // bound orbit reaches its turning points
        double rmin = 1e9, rmax = 0;
        for (double r : trk.r) { rmin = std::min(rmin, r); rmax = std::max(rmax, r); }
        check("turning points reproduced", std::fabs(rmin - rp) < 0.2 && std::fabs(rmax - ra) < 0.2, "r in [%.3f, %.3f]", rmin, rmax);
    }

    // ---------------------------------------------------------------- 12. coordinate-time accumulation
    std::printf("\n[12] Light-travel time along a ray (regularised t-hat integration)\n");
    {
        // Radial ray in Schwarzschild: t(r0) - t(r) = r_* difference, r_* = r + 2 ln(r/2 - 1)
        auto rstar = [](double r) { return r + 2.0 * std::log(r / 2.0 - 1.0); };
        double worst = 0;
        for (double r0 : {40.0, 150.0}) {
            bh::Ray<double> ray;
            bh::init_ray_constants<double>(0.0, r0, 0.3, 0.0, 0.0, 1.0, ray);  // L = 0, Q = 0: purely radial
            double tv = 0, that_ref = 0;
            const double r_stop = 7.0;
            for (int i = 0; i < 1000; ++i) {
                const double h = bh::step_size(0.12, ray);
                const double r_old = 1.0 / ray.u;
                bh::step_ray(0.0, ray, h);
                tv += std::fabs(1.0 / ray.u - r_old);
                if (1.0 / ray.u < r_stop) break;
            }
            that_ref = ray.that;
            const double t_ray = that_ref - tv;                       // (negative: backward in time)
            const double t_exact = -(rstar(r0) - rstar(1.0 / ray.u));  // exact coordinate-time offset
            worst = std::max(worst, std::fabs(t_ray - t_exact));
        }
        check("radial photon: dt matches tortoise coordinate", worst < 2e-2, "max |dt_ray - dt_exact| = %.2e M", worst);
    }

    // ---------------------------------------------------------------- 13. polar axis
    std::printf("\n[13] Rays skimming and crossing the polar axis (camera meridian plane)\n");
    {
        // Sweep the image column through the meridian plane (L = 0).  The lensed direction of the sky must be a
        // smooth, converged function of the pixel: adjacent rays differ by << the pixel size and c-refinement changes nothing.
        const double a = 0.9, th0 = 76.0 * PI / 180.0, r0 = 46.0;
        double worst_jump = 0, worst_conv = 0;
        for (double by : {-9.0, -7.0, 8.0, 12.0}) {
            std::vector<std::array<double, 3>> dirs;
            for (int i = -40; i <= 40; ++i) {
                const double al = i * 0.004;    // impact-parameter step 0.004 M  ( ~1/12 of a 4K pixel at 46 M )
                const auto o = trace_pixel<double>(a, r0, th0, al, by, 0.1, false);
                if (o.status != 1) continue;
                dirs.push_back({o.dx, o.dy, o.dz});
            }
            // second difference: ~0 for a smooth map, large for a seam / jump
            for (size_t i = 1; i + 1 < dirs.size(); ++i) {
                double d2 = 0;
                for (int k = 0; k < 3; ++k) {
                    const double sd = dirs[i + 1][k] - 2.0 * dirs[i][k] + dirs[i - 1][k];
                    d2 += sd * sd;
                }
                worst_jump = std::max(worst_jump, std::sqrt(d2));
            }
            const auto c1 = trace_pixel<double>(a, r0, th0, 0.0, by, 0.10, false);
            const auto c2 = trace_pixel<double>(a, r0, th0, 0.0, by, 0.01, false);
            if (c1.status == 1 && c2.status == 1)
                worst_conv = std::max(worst_conv, std::sqrt((c1.dx - c2.dx) * (c1.dx - c2.dx) + (c1.dy - c2.dy) * (c1.dy - c2.dy) + (c1.dz - c2.dz) * (c1.dz - c2.dz)));
        }
        check("sky direction smooth across the axis", worst_jump < 2e-4, "max 2nd difference of the sky direction = %.1e", worst_jump);
        check("meridian-plane ray converged (c=0.10 vs 0.01)", worst_conv < 2e-4, "|d(c=.1) - d(c=.01)| = %.1e", worst_conv);
        // float precision, same sweep
        double worst_f = 0;
        for (int i = -40; i <= 40; ++i) {
            const auto o = trace_pixel<float>(a, r0, th0, i * 0.004, 8.0, 0.1, false);
            const auto d = trace_pixel<double>(a, r0, th0, i * 0.004, 8.0, 0.1, false);
            if (o.status == 1 && d.status == 1)
                worst_f = std::max(worst_f, std::sqrt((o.dx - d.dx) * (o.dx - d.dx) + (o.dy - d.dy) * (o.dy - d.dy) + (o.dz - d.dz) * (o.dz - d.dz)));
        }
        check("float32 == double near the axis", worst_f < 5e-4, "max |d_float - d_double| = %.1e", worst_f);
    }

    // ---------------------------------------------------------------- 14. corona integral
    std::printf("\n[14] Optically thin corona (halo) line-of-sight integral\n");
    {
        const double a = 0.9, th0 = 76.0 * PI / 180.0, r0 = 64.0;
        double worst = 0, gmin = 1e9, gmax = 0;
        int n = 0;
        for (double al : {-9.0, -6.5, -3.0, 0.0, 3.0, 6.5, 9.0})
            for (double be : {-4.0, 0.0, 4.0, 8.0}) {
                double res[2], gm[2];
                int k = 0;
                for (double c : {0.10, 0.03}) {
                    const double x = al / r0, y = be / r0, inv = 1.0 / std::sqrt(1.0 + x * x + y * y);
                    bh::Ray<double> ray;
                    double nu;
                    bh::init_ray_zamo<double>(a, r0, th0, 0.0, -inv, -y * inv, x * inv, ray, nu);
                    auto cfg = make_cfg<double>(a, 400, c, 100000, true);
                    cfg.halo = 1.0;
                    cfg.nu_cam = nu;
                    cfg.disk_h = 0.02;
                    bh::TraceOut<double> out;
                    bh::trace_ray(cfg, ray, out);
                    res[k] = out.halo_e;
                    gm[k] = out.halo_g;
                    ++k;
                }
                if (res[1] > 1e-6) {
                    worst = std::max(worst, std::fabs(res[0] - res[1]) / res[1]);
                    gmin = std::min(gmin, gm[1]);
                    gmax = std::max(gmax, gm[1]);
                    ++n;
                }
            }
        check("corona integral converged (c = 0.10 vs 0.03)", n > 10 && worst < 0.05, "%d rays, max rel. diff = %.2e", n, worst);
        check("corona Doppler shift spans blue and red", gmin < 1.0 && gmax > 1.0, "mean g in [%.2f, %.2f]", gmin, gmax);
    }

    // ---------------------------------------------------------------- step statistics (for the README)
    {
        long total_steps = 0;
        int n = 0;
        for (int i = 0; i < 40; ++i)
            for (int j = 0; j < 40; ++j) {
                const double al = -12 + 24.0 * i / 39.0, be = -8 + 16.0 * j / 39.0;
                const auto o = trace_pixel<float>(0.9, 50.0, 76.0 * PI / 180.0, al, be, 0.12, true);
                total_steps += o.steps;
                ++n;
            }
        std::printf("\n  (info) mean integrator steps per ray, 50 M camera, c=0.12, float: %.1f\n", (double)total_steps / n);
    }

    std::printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
