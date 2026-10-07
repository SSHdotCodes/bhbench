// Validation suite for the Kerr physics. Everything here runs in double precision on the CPU
// (the GPU path is float-only, so it is checked separately against this suite's results).
//
// Build and run with `make test`.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "shared_types.h"
#include "kerr_core.h"
#include "disk_model.h"
#include "funnel_model.h"

namespace {

int g_fail = 0;
int g_pass = 0;

void report(const char* name, bool ok, const char* fmt, double v1 = 0.0, double v2 = 0.0) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
    }
    std::printf("[%s] %-44s ", ok ? "PASS" : "FAIL", name);
    std::printf(fmt, v1, v2);
    std::printf("\n");
}

typedef V3<double> D3;

// Trace parameters for the pure-photon tests: the disk is placed where no photon can land.
TraceParams<double> photon_only_params(double a, double D, double eta) {
    TraceParams<double> tp;
    tp.a = a;
    tp.r_plus = 1.0 + std::sqrt(1.0 - a * a);
    tp.r_capture = capture_radius(a);
    tp.r_isco = 1e6;
    tp.r_out = 2e6;
    tp.eta = eta;
    tp.r_escape = 2.0 * D + 20.0;
    tp.cam_ut = 1.0;
    tp.max_steps = 400000;
    return tp;
}

// Initial phase point at (-D, y0, 0) travelling in +x, with the null condition solved for p_x.
Phase<double> launch(double a, double D, double y0) {
    Phase<double> s;
    s.x = v3(-D, y0, 0.0);
    double lo = 0.8, hi = 1.2;
    for (int i = 0; i < 100; ++i) {
        double mid = 0.5 * (lo + hi);
        s.p = v3(mid, 0.0, 0.0);
        if (null_residual(a, s) < 0.0) lo = mid; else hi = mid;
    }
    s.p = v3(0.5 * (lo + hi), 0.0, 0.0);
    return s;
}

// Bisects on the impact parameter y0 between a captured value and an escaping value.
double critical_impact(double a, double D, double y_cap, double y_esc, double eta) {
    TraceParams<double> tp = photon_only_params(a, D, eta);
    double lo = y_cap, hi = y_esc;
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        Hit<double> h = trace_photon(tp, launch(a, D, mid));
        if (h.outcome == OUT_CAPTURED) lo = mid; else hi = mid;
    }
    Phase<double> s = launch(a, D, 0.5 * (lo + hi));
    return photon_L(s);
}

void test_metric_inverse() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(-6.0, 6.0);
    double worst = 0.0;
    for (int trial = 0; trial < 500; ++trial) {
        double a = 0.95;
        D3 x = v3(U(rng), U(rng), U(rng));
        KSData<double> k = ks_eval(a, x);
        double gcov[4][4], gup[4][4];
        double l_lo[4] = {1.0, k.l[0], k.l[1], k.l[2]};
        double l_up[4] = {-1.0, k.l[0], k.l[1], k.l[2]};
        double eta[4] = {-1.0, 1.0, 1.0, 1.0};
        for (int m = 0; m < 4; ++m) {
            for (int n = 0; n < 4; ++n) {
                gcov[m][n] = (m == n ? eta[m] : 0.0) + k.H * l_lo[m] * l_lo[n];
                gup[m][n] = (m == n ? eta[m] : 0.0) - k.H * l_up[m] * l_up[n];
            }
        }
        for (int m = 0; m < 4; ++m) {
            for (int n = 0; n < 4; ++n) {
                double s = 0.0;
                for (int q = 0; q < 4; ++q) s += gcov[m][q] * gup[q][n];
                worst = std::fmax(worst, std::fabs(s - (m == n ? 1.0 : 0.0)));
            }
        }
    }
    report("metric inverse g^{mu a} g_{a nu} = delta", worst < 1e-12, "max error %.3e", worst);
}

void test_null_and_radius() {
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> U(-8.0, 8.0);
    double worst_null = 0.0, worst_impl = 0.0;
    for (int trial = 0; trial < 500; ++trial) {
        double a = 0.3 + 0.65 * (trial % 7) / 6.0;
        D3 x = v3(U(rng), U(rng), U(rng));
        KSData<double> k = ks_eval(a, x);
        worst_null = std::fmax(worst_null, std::fabs(k.l[0] * k.l[0] + k.l[1] * k.l[1] + k.l[2] * k.l[2] - 1.0));
        double rho2 = x.x * x.x + x.y * x.y;
        double F = rho2 / (k.r * k.r + a * a) + x.z * x.z / (k.r * k.r) - 1.0;
        worst_impl = std::fmax(worst_impl, std::fabs(F));
    }
    report("l_mu null: sum l_i^2 = 1 (eta-null)", worst_null < 1e-12, "max error %.3e", worst_null);
    report("Kerr-Schild r satisfies implicit equation", worst_impl < 1e-12, "max residual %.3e", worst_impl);
}

void test_gradients() {
    std::mt19937 rng(13);
    std::uniform_real_distribution<double> U(-5.0, 5.0);
    double worst = 0.0;
    for (int trial = 0; trial < 200; ++trial) {
        double a = 0.9;
        D3 x = v3(U(rng), U(rng), U(rng));
        KSData<double> k = ks_eval(a, x);
        double h = 1e-6;
        for (int i = 0; i < 3; ++i) {
            D3 xp = x, xm = x;
            double* cp = (i == 0) ? &xp.x : (i == 1) ? &xp.y : &xp.z;
            double* cm = (i == 0) ? &xm.x : (i == 1) ? &xm.y : &xm.z;
            *cp += h;
            *cm -= h;
            KSData<double> kp = ks_eval(a, xp), km = ks_eval(a, xm);
            double dr = (kp.r - km.r) / (2 * h);
            double dH = (kp.H - km.H) / (2 * h);
            worst = std::fmax(worst, std::fabs(dr - k.dr[i]) / (1.0 + std::fabs(dr)));
            worst = std::fmax(worst, std::fabs(dH - k.dH[i]) / (1.0 + std::fabs(dH)));
            for (int j = 0; j < 3; ++j) {
                double dl = (kp.l[j] - km.l[j]) / (2 * h);
                worst = std::fmax(worst, std::fabs(dl - k.dl[i][j]) / (1.0 + std::fabs(dl)));
            }
        }
    }
    report("analytic gradients match finite differences", worst < 1e-6, "max relative error %.3e", worst);
}

void test_null_geodesic_conservation() {
    // An escaping Kerr photon (a = 0.9) that passes through the photon-sphere region, integrated with
    // the same adaptive RK4 the tracer uses, from the observer at D = 30 out to r = 60.
    double a = 0.9;
    double D = 30.0;
    Phase<double> s = launch(a, D, 4.0);
    double L0 = photon_L(s);
    double worst_null = 0.0, worst_L = 0.0;
    int steps = 0;
    double rmin = 1e9;
    for (; steps < 200000; ++steps) {
        double R = len3(s.x);
        rmin = std::fmin(rmin, ks_radius(a, s.x));
        if (R > 60.0 && dot3(s.x, s.p) > 0.0) break;
        Phase<double> k1 = ray_rhs(a, s);
        s = rk4_step_k1(a, s, k1, step_length(0.02, R, s, k1));
        worst_null = std::fmax(worst_null, std::fabs(null_residual(a, s)));
        worst_L = std::fmax(worst_L, std::fabs(photon_L(s) - L0));
    }
    report("escaping photon stays null (a=0.9, eta=0.02)", worst_null < 1e-6, "max |g p p| %.3e, min r %.3f", worst_null, rmin);
    std::printf("         (%d RK4 steps to escape)\n", steps);
    report("photon L = y p_x - x p_y conserved", worst_L < 1e-6, "max |dL| %.3e", worst_L);
}

void test_observer_frame() {
    double a = 0.7;
    D3 pos = v3(-20.0, 3.0, 9.0);
    Observer<double> o = make_observer(a, pos, v3(0.0, 1.0, 0.0), v3(0.0, 0.0, 1.0), v3(1.0, 0.0, 0.0));
    KSData<double> k = ks_eval(a, pos);
    double worst = 0.0;
    // Orthonormal: g(e_i, e_j) = delta_ij, and orthogonal to u.
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // Recover contravariant e from covariant: g(e_i, e_j) = e_i^mu e_j_mu, with e_j_mu known.
            double ei_up[4];
            // Solve e^mu from e_mu using the inverse metric g^{mu nu} = eta - H l l.
            double lu[4] = {-1.0, k.l[0], k.l[1], k.l[2]};
            double ej_cov[4] = {o.e[j][0], o.e[j][1], o.e[j][2], o.e[j][3]};
            double ei_cov[4] = {o.e[i][0], o.e[i][1], o.e[i][2], o.e[i][3]};
            double eta[4] = {-1.0, 1.0, 1.0, 1.0};
            for (int m = 0; m < 4; ++m) {
                double s = 0.0;
                for (int n = 0; n < 4; ++n) {
                    double gup = (m == n ? eta[m] : 0.0) - k.H * lu[m] * lu[n];
                    s += gup * ej_cov[n];
                }
                ei_up[m] = s;
            }
            double inner = 0.0;
            for (int m = 0; m < 4; ++m) inner += ei_up[m] * ei_cov[m];
            worst = std::fmax(worst, std::fabs(inner - (i == j ? 1.0 : 0.0)));
        }
        double uu = 0.0;
        double uup[4];
        double lu[4] = {-1.0, k.l[0], k.l[1], k.l[2]};
        double eta[4] = {-1.0, 1.0, 1.0, 1.0};
        for (int m = 0; m < 4; ++m) {
            double s = 0.0;
            for (int n = 0; n < 4; ++n) s += ((m == n ? eta[m] : 0.0) - k.H * lu[m] * lu[n]) * o.u[n];
            uup[m] = s;
        }
        for (int m = 0; m < 4; ++m) uu += uup[m] * o.e[i][m];
        worst = std::fmax(worst, std::fabs(uu));
    }
    report("static observer triad orthonormal, u-orthogonal", worst < 1e-10, "max error %.3e", worst);
}

void test_pixel_null() {
    double a = 0.5;
    Observer<double> o = make_observer(a, v3(-25.0, 0.0, 0.0), v3(0.0, -1.0, 0.0), v3(0.0, 0.0, 1.0), v3(1.0, 0.0, 0.0));
    double worst_null = 0.0;
    for (int i = -4; i <= 4; ++i) {
        for (int j = -4; j <= 4; ++j) {
            Phase<double> s = pixel_ray(o, 0.1 * i, 0.1 * j);
            worst_null = std::fmax(worst_null, std::fabs(null_residual(a, s)));
        }
    }
    report("pixel rays are null (after E = 1 normalisation)", worst_null < 1e-10, "max residual %.3e", worst_null);
}

void test_isco_and_orbits() {
    report("ISCO r = 6M for a = 0 (Schwarzschild)", std::fabs(isco_radius(0.0) - 6.0) < 1e-12, "r = %.12f", isco_radius(0.0));
    report("ISCO r = 2.32088 for a = 0.9", std::fabs(isco_radius(0.9) - 2.320883) < 1e-5, "r = %.8f", isco_radius(0.9));
    report("ISCO efficiency a=0: 1 - E = 1 - sqrt(8/9)", std::fabs((1.0 - circular_orbit(0.0, 6.0).E) - (1.0 - std::sqrt(8.0 / 9.0))) < 1e-12,
           "E = %.12f", circular_orbit(0.0, 6.0).E);
    report("L_isco = 2 sqrt(3) for a = 0", std::fabs(circular_orbit(0.0, 6.0).L - 2.0 * std::sqrt(3.0)) < 1e-12, "L = %.12f", circular_orbit(0.0, 6.0).L);

    // Consistency of E, L, Omega, u^t with the Boyer-Lindquist Kerr metric on the equator.
    double worst = 0.0;
    for (double a : {0.0, 0.5, 0.9, 0.998}) {
        for (double r : {isco_radius(a) + 0.1, 4.0, 10.0, 40.0}) {
            CircularOrbit o = circular_orbit(a, r);
            double gtt = -(1.0 - 2.0 / r);
            double gtp = -2.0 * a / r;
            double gpp = r * r + a * a + 2.0 * a * a / r;
            double norm = o.ut * o.ut * (gtt + 2.0 * gtp * o.Omega + gpp * o.Omega * o.Omega) + 1.0;
            double E = -o.ut * (gtt + gtp * o.Omega);
            double L = o.ut * (gtp + gpp * o.Omega);
            worst = std::fmax(worst, std::fabs(norm));
            worst = std::fmax(worst, std::fabs(E - o.E) / std::fabs(o.E));
            worst = std::fmax(worst, std::fabs(L - o.L) / std::fabs(o.L));
        }
    }
    report("circular orbit: u.u = -1, E = -u_t, L = u_phi", worst < 1e-10, "max error %.3e", worst);

    // The ISCO is where dE/dr = 0.
    double a = 0.9, r = isco_radius(a), h = 1e-5;
    double dE = (circular_orbit(a, r + h).E - circular_orbit(a, r - h).E) / (2 * h);
    report("dE/dr vanishes at the ISCO (a = 0.9)", std::fabs(dE) < 1e-6, "dE/dr = %.3e", dE);
}

void test_nt_energy_identity() {
    // int_{r_isco}^{inf} E f dr must equal 1 - E_isco (radiated energy per unit rest energy).
    for (double a : {0.0, 0.9}) {
        double ri = isco_radius(a);
        int n = 400000;
        double Rbig = 1e4;
        std::vector<double> rg(n);
        for (int k = 0; k < n; ++k) {
            rg[k] = ri * std::pow(Rbig / ri, double(k) / double(n - 1));
        }
        std::vector<double> f = nt_f_on_grid(a, rg);
        double integral = 0.0;
        for (int k = 1; k < n; ++k) {
            double e0 = circular_orbit(a, rg[k - 1]).E * f[k - 1];
            double e1 = circular_orbit(a, rg[k]).E * f[k];
            integral += 0.5 * (e0 + e1) * (rg[k] - rg[k - 1]);
        }
        integral += 1.5 / Rbig;   // tail: f -> 3 / (2 r^2) at large r, E -> 1
        double expect = 1.0 - circular_orbit(a, ri).E;
        char name[64];
        std::snprintf(name, sizeof name, "NT energy identity, a = %.1f", a);
        report(name, std::fabs(integral - expect) < 2e-4, "integral %.6f vs 1 - E_isco %.6f", integral, expect);
    }
}

void test_flamm_embedding() {
    // a = 0: Y(r) - Y(r_view) must equal 2 sqrt(2 (r - 2)) - 2 sqrt(2 (r_view - 2)) relative to the rim.
    FunnelProfile p = build_funnel_profile(0.0, 14.0, 4000);
    double worst = 0.0;
    double flammRim = 2.0 * std::sqrt(2.0 * (14.0 - 2.0));
    for (size_t k = 0; k < p.r.size(); ++k) {
        double flamm = 2.0 * std::sqrt(2.0 * (p.r[k] - 2.0)) - flammRim;
        worst = std::fmax(worst, std::fabs(flamm - p.Y[k]));
    }
    report("rubber sheet reproduces Flamm's paraboloid (a=0)", worst < 1e-3, "max |dY| %.3e", worst);
}

void test_photon_orbit_closed_form() {
    double worst = 0.0;
    for (double a : {0.0, 0.3, 0.6, 0.9, 0.998}) {
        worst = std::fmax(worst, std::fabs(photon_orbit_prograde_radius(a) - photon_orbit_prograde(a)));
    }
    report("prograde photon orbit: closed form = root", worst < 1e-9, "max error %.3e", worst);
}

void test_schwarzschild_shadow() {
    // Critical impact parameter for capture: b_c = 3 sqrt(3) M.
    double bc = critical_impact(0.0, 200.0, 4.5, 6.0, 0.02);
    report("Schwarzschild critical b = 3 sqrt(3) M", std::fabs(bc - 3.0 * std::sqrt(3.0)) < 2e-4, "b_c = %.6f", bc);
}

void test_kerr_critical_impact() {
    // Equatorial critical impact parameters from the circular photon orbits (Bardeen 1972):
    //   xi(r) = -(r^3 - 3 r^2 + a^2 r + a^2) / (a (r - 1)), with r the photon-orbit radius.
    double a = 0.9;
    double rp = photon_orbit_prograde(a);
    double xi_pro = -(rp * rp * rp - 3.0 * rp * rp + a * a * rp + a * a) / (a * (rp - 1.0));
    double pro = critical_impact(a, 200.0, 2.0, 3.5, 0.02);
    report("Kerr a=0.9 prograde critical L (co-rotating)", std::fabs(pro - xi_pro) < 2e-4, "L_c = %.6f vs formula %.6f", pro, xi_pro);

    // Retrograde: photon orbit solves r^{3/2} - 3 r^{1/2} - 2a = 0.
    double lo = 1.5, hi = 4.5;
    for (int i = 0; i < 200; ++i) {
        double mid = 0.5 * (lo + hi);
        double f = std::pow(mid, 1.5) - 3.0 * std::sqrt(mid) - 2.0 * a;
        if (f < 0.0) lo = mid; else hi = mid;
    }
    double rr = 0.5 * (lo + hi);
    double xi_retro = -(rr * rr * rr - 3.0 * rr * rr + a * a * rr + a * a) / (a * (rr - 1.0));
    // Photons with negative L (counter-rotating). Captured at |y| = 6.5, escaping at |y| = 7.2.
    double retro = critical_impact(a, 200.0, -6.5, -7.2, 0.02);
    report("Kerr a=0.9 retrograde critical L (counter)", std::fabs(retro - xi_retro) < 5e-4, "L_c = %.6f vs formula %.6f", retro, xi_retro);
    report("prograde photons get closer than retrograde", std::fabs(pro) < std::fabs(retro), "|L_pro| = %.4f < |L_retro| = %.4f", std::fabs(pro), std::fabs(retro));
}

void test_float_double_agreement() {
    // The GPU runs the same code in float. Compare outcomes and final directions over a grid of pixels.
    double a = 0.9;
    double D = 40.0;
    Observer<double> od = make_observer(a, v3(-D * 0.8, 0.0, D * 0.6), v3(0.6, 0.0, 0.8), v3(0.0, 1.0, 0.0), v3(0.8, 0.0, -0.6));
    Observer<float> of = make_observer(float(a), v3(float(-D * 0.8), 0.0f, float(D * 0.6)), v3(0.6f, 0.0f, 0.8f), v3(0.0f, 1.0f, 0.0f), v3(0.8f, 0.0f, -0.6f));
    TraceParams<double> tpd = photon_only_params(a, D, 0.03);
    TraceParams<float> tpf;
    tpf.a = float(a);
    tpf.r_plus = float(tpd.r_plus);
    tpf.r_capture = float(tpd.r_capture);
    tpf.r_isco = 1e6f;
    tpf.r_out = 2e6f;
    tpf.eta = 0.03f;
    tpf.r_escape = float(tpd.r_escape);
    tpf.cam_ut = float(of.ut);
    tpf.max_steps = 400000;
    int mismatch = 0, total = 0;
    double worst_dir = 0.0;
    for (int i = -20; i <= 20; ++i) {
        for (int j = -20; j <= 20; ++j) {
            double sx = 0.02 * i, sy = 0.02 * j;
            Hit<double> hd = trace_photon(tpd, pixel_ray(od, sx, sy));
            Hit<float> hf = trace_photon(tpf, pixel_ray(of, float(sx), float(sy)));
            ++total;
            if (hd.outcome != hf.outcome) {
                ++mismatch;
                continue;
            }
            if (hd.outcome == OUT_ESCAPED) {
                double dd = std::fabs(hd.dir.x - hf.dir.x) + std::fabs(hd.dir.y - hf.dir.y) + std::fabs(hd.dir.z - hf.dir.z);
                worst_dir = std::fmax(worst_dir, dd);
            }
        }
    }
    report("float32 vs float64 outcomes agree (GPU precision)", mismatch <= total / 200, "%.0f of %.0f pixels disagree", double(mismatch), double(total));
    report("float32 vs float64 escape directions", worst_dir < 2e-3, "max |dn|_1 %.3e", worst_dir);
}

void test_redshift_limits() {
    // A photon emitted by the ISCO toward a distant observer on the axis: g = 1/(u^t (1 - Omega L / E))
    // with L = 0 gives g = 1/u^t. Schwarzschild ISCO u^t = 1/sqrt(1 - 3/6) = sqrt(2).
    CircularOrbit o = circular_orbit(0.0, 6.0);
    report("Schwarzschild ISCO u^t = sqrt(2)", std::fabs(o.ut - std::sqrt(2.0)) < 1e-12, "u^t = %.12f", o.ut);
}

}  // namespace

int main() {
    std::printf("Kerr black hole physics validation (double precision)\n\n");
    test_metric_inverse();
    test_null_and_radius();
    test_gradients();
    test_null_geodesic_conservation();
    test_observer_frame();
    test_pixel_null();
    test_isco_and_orbits();
    test_nt_energy_identity();
    test_flamm_embedding();
    test_photon_orbit_closed_form();
    test_schwarzschild_shadow();
    test_kerr_critical_impact();
    test_float_double_agreement();
    test_redshift_limits();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
