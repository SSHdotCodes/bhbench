#include "physics.hpp"
#include "kerr_shared.h"

#include <algorithm>
#include <cmath>

namespace phys {

double r_horizon(double a) { return bh::horizon_radius<double>(a); }
double r_isco(double a) { return bh::isco_radius<double>(a); }
double r_photon_pro(double a) { return 2.0 * (1.0 + std::cos(2.0 / 3.0 * std::acos(-a))); }
double r_photon_retro(double a) { return 2.0 * (1.0 + std::cos(2.0 / 3.0 * std::acos(a))); }
double r_ergo_equator() { return 2.0; }

Circ circular(double r, double a)
{
    const double sr = std::sqrt(r), r15 = r * sr, r34 = std::pow(r, 0.75);
    const double den = r34 * std::sqrt(r15 - 3.0 * sr + 2.0 * a);
    Circ c;
    c.omega = 1.0 / (r15 + a);
    c.E = (r15 - 2.0 * sr + a) / den;
    c.L = (r * r - 2.0 * a * sr + a * a) / den;
    c.ut = (r15 + a) / den;
    return c;
}

// ---------------------------------------------------------------------------------------------
// Novikov-Thorne flux
// ---------------------------------------------------------------------------------------------
static double dLdr(double r, double a)
{
    const double h = 1e-6 * r;
    return (circular(r + h, a).L - circular(r - h, a).L) / (2.0 * h);
}
static double nt_integrand(double r, double a)
{
    const Circ c = circular(r, a);
    return (c.E - c.omega * c.L) * dLdr(r, a);
}
static double nt_prefactor(double r, double a)
{
    const Circ c = circular(r, a);
    const double domega = 1.5 * std::sqrt(r) * c.omega * c.omega;  // -dOmega/dr (> 0)
    const double x = c.E - c.omega * c.L;
    return domega / (r * x * x);
}

double nt_flux_numeric(double r, double a)
{
    const double rin = r_isco(a);
    if (r <= rin) return 0.0;
    const int n = 4000;  // Simpson
    const double h = (r - rin) / n;
    double s = nt_integrand(rin, a) + nt_integrand(r, a);
    for (int i = 1; i < n; ++i) s += nt_integrand(rin + i * h, a) * ((i & 1) ? 4.0 : 2.0);
    const double integral = s * h / 3.0;
    return nt_prefactor(r, a) * integral;
}

double nt_flux_closed(double r, double a)
{
    const double x = std::sqrt(r), x0 = std::sqrt(r_isco(a));
    const double ac = std::acos(a);
    const double x1 = 2.0 * std::cos(ac / 3.0 - PI / 3.0);
    const double x2 = 2.0 * std::cos(ac / 3.0 + PI / 3.0);
    const double x3 = -2.0 * std::cos(ac / 3.0);
    double br = x - x0 - 1.5 * a * std::log(x / x0);
    br -= 3.0 * (x1 - a) * (x1 - a) / (x1 * (x1 - x2) * (x1 - x3)) * std::log((x - x1) / (x0 - x1));
    br -= 3.0 * (x2 - a) * (x2 - a) / (x2 * (x2 - x1) * (x2 - x3)) * std::log((x - x2) / (x0 - x2));
    br -= 3.0 * (x3 - a) * (x3 - a) / (x3 * (x3 - x1) * (x3 - x2)) * std::log((x - x3) / (x0 - x3));
    return 1.5 * br / (x * x * x * x * (x * x * x - 3.0 * x + 2.0 * a));
}

std::vector<float> nt_temperature_table(double a, double r_out, int n, double* fmax_out)
{
    const double rin = r_isco(a);
    std::vector<double> F(n, 0.0);
    // cumulative Simpson of the integral, sub-stepped
    double acc = 0.0;
    double rprev = rin;
    for (int i = 1; i < n; ++i) {
        const double r = rin + (r_out - rin) * i / (n - 1);
        const int sub = 24;
        const double h = (r - rprev) / sub;
        double s = nt_integrand(rprev, a) + nt_integrand(r, a);
        for (int k = 1; k < sub; ++k) s += nt_integrand(rprev + k * h, a) * ((k & 1) ? 4.0 : 2.0);
        acc += s * h / 3.0;
        F[i] = std::max(0.0, nt_prefactor(r, a) * acc);
        rprev = r;
    }
    double fmax = 0.0;
    for (double f : F) fmax = std::max(fmax, f);
    if (fmax_out) *fmax_out = fmax;
    std::vector<float> T(n);
    for (int i = 0; i < n; ++i) T[i] = (float)std::pow(F[i] / fmax, 0.25);
    return T;
}

// ---------------------------------------------------------------------------------------------
// Blackbody -> sRGB
// ---------------------------------------------------------------------------------------------
static double lobe(double l, double mu, double s1, double s2)
{
    const double t = (l - mu) / (l < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}
static void cie_xyz(double l, double* x, double* y, double* z)
{
    *x = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);
    *y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);
    *z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);
}
static void planck_xyz(double T, double* X, double* Y, double* Z)
{
    double sx = 0, sy = 0, sz = 0;
    const double c2 = 1.438777e7;  // hc/k in nm K
    for (double l = 360.0; l <= 830.0; l += 1.0) {
        const double e = c2 / (l * T);
        const double b = (e > 600.0) ? 0.0 : 1.0 / (std::pow(l, 5.0) * (std::exp(e) - 1.0));
        double x, y, z;
        cie_xyz(l, &x, &y, &z);
        sx += b * x; sy += b * y; sz += b * z;
    }
    *X = sx; *Y = sy; *Z = sz;
}

double blackbody_luminance(double T)
{
    double xr, yr, zr, X, Y, Z;
    planck_xyz(6504.0, &xr, &yr, &zr);
    planck_xyz(T, &X, &Y, &Z);
    return Y / yr;
}

std::vector<BBRow> blackbody_table(int n, double Tmin, double Tmax)
{
    double xr, yr, zr;
    planck_xyz(6504.0, &xr, &yr, &zr);
    std::vector<BBRow> t(n);
    for (int i = 0; i < n; ++i) {
        const double T = Tmin * std::pow(Tmax / Tmin, (double)i / (n - 1));
        double X, Y, Z;
        planck_xyz(T, &X, &Y, &Z);
        BBRow row{0.f, 0.f, 0.f, -60.f};
        if (Y > 1e-300) {
            const double xn = X / Y, zn = Z / Y;  // Y = 1
            double R = 3.2406 * xn - 1.5372 - 0.4986 * zn;
            double G = -0.9689 * xn + 1.8758 + 0.0415 * zn;
            double B = 0.0557 * xn - 0.2040 + 1.0570 * zn;
            R = std::max(R, 0.0); G = std::max(G, 0.0); B = std::max(B, 0.0);
            const double lum = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            row.r = (float)(R / lum); row.g = (float)(G / lum); row.b = (float)(B / lum);
            row.log2y = (float)std::max(std::log2(Y / yr), -60.0);
        }
        t[i] = row;
    }
    return t;
}

// ---------------------------------------------------------------------------------------------
// Embedding diagram
// ---------------------------------------------------------------------------------------------
Embedding kerr_embedding(double a, double r_max, int n)
{
    Embedding e;
    const double rp = r_horizon(a), rm = 2.0 - rp;
    const double dr = rp - rm;
    const double wmax = std::sqrt(r_max - rp);
    e.r.resize(n); e.rho.resize(n); e.z.resize(n);
    std::vector<double> g(n);
    for (int i = 0; i < n; ++i) {
        const double w = wmax * i / (n - 1);
        const double r = rp + w * w;
        const double rho = std::sqrt(r * r + a * a + 2.0 * a * a / r);
        const double drho = (r - a * a / (r * r)) / rho;
        const double rad = r * r / (w * w + dr) - w * w * drho * drho;
        if (rad < 0.0) e.valid = false;
        g[i] = 2.0 * std::sqrt(std::max(rad, 0.0));
        e.r[i] = r;
        e.rho[i] = rho;
    }
    std::vector<double> C(n, 0.0);
    const double dw = wmax / (n - 1);
    for (int i = 1; i < n; ++i) C[i] = C[i - 1] + 0.5 * (g[i] + g[i - 1]) * dw;
    for (int i = 0; i < n; ++i) e.z[i] = C[i] - C[n - 1];
    e.throat_rho = e.rho[0];
    return e;
}

void bardeen_LQ(double a, double r, double* L, double* Q)
{
    *L = -(r * r * r - 3.0 * r * r + a * a * r + a * a) / (a * (r - 1.0));
    *Q = r * r * r * (4.0 * a * a - r * (r - 3.0) * (r - 3.0)) / (a * a * (r - 1.0) * (r - 1.0));
}

void bardeen_curve(double a, double th0, double r, double* alpha, double* beta)
{
    const double xi = -(r * r * r - 3.0 * r * r + a * a * r + a * a) / (a * (r - 1.0));
    const double eta = r * r * r * (4.0 * a * a - r * (r - 3.0) * (r - 3.0)) / (a * a * (r - 1.0) * (r - 1.0));
    const double s = std::sin(th0), c = std::cos(th0);
    *alpha = -xi / s;
    *beta = std::sqrt(std::max(eta + a * a * c * c - xi * xi * c * c / (s * s), 0.0));
}

// ---------------------------------------------------------------------------------------------
// Timelike equatorial geodesics
// ---------------------------------------------------------------------------------------------
static double Rt(double a, double r, double E, double L)
{
    const double X = r * r + a * a, D = r * r - 2.0 * r + a * a;
    const double P = E * X - a * L;
    return P * P - D * (r * r + (L - a * E) * (L - a * E));
}

double energy_from_apoapsis(double a, double r, double L)
{
    const double X = r * r + a * a, D = r * r - 2.0 * r + a * a;
    const double A = X * X - D * a * a;
    const double B = -4.0 * a * L * r;
    const double C = (2.0 * r - r * r) * L * L - D * r * r;
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return -1.0;
    return (-B + std::sqrt(disc)) / (2.0 * A);
}

bool bound_orbit_constants(double a, double rp, double ra, double* Eo, double* Lo)
{
    const double p = 2.0 * ra * rp / (ra + rp), e = (ra - rp) / (ra + rp);
    double E = std::sqrt(std::max(((p - 2.0) * (p - 2.0) - 4.0 * e * e) / (p * (p - 3.0 - e * e)), 1e-3));
    double L = p / std::sqrt(std::max(p - 3.0 - e * e, 1e-3));
    for (int it = 0; it < 60; ++it) {
        const double f1 = Rt(a, rp, E, L), f2 = Rt(a, ra, E, L);
        const double dE = 1e-7, dL = 1e-7;
        const double j11 = (Rt(a, rp, E + dE, L) - f1) / dE, j12 = (Rt(a, rp, E, L + dL) - f1) / dL;
        const double j21 = (Rt(a, ra, E + dE, L) - f2) / dE, j22 = (Rt(a, ra, E, L + dL) - f2) / dL;
        const double det = j11 * j22 - j12 * j21;
        if (std::fabs(det) < 1e-300) return false;
        const double sE = (f1 * j22 - f2 * j12) / det, sL = (j11 * f2 - j21 * f1) / det;
        E -= sE; L -= sL;
        if (std::fabs(sE) + std::fabs(sL) < 1e-13) {
            *Eo = E; *Lo = L;
            return E > 0.0 && E < 1.0 && L > 0.0;
        }
    }
    return false;
}

void Track::sample(double t, bool loop, double* ro, double* po) const
{
    const double T = duration();
    if (r.empty()) { *ro = 10.0; *po = 0.0; return; }
    if (loop && T > 0.0) {
        t = std::fmod(t, T);
        if (t < 0.0) t += T;
    }
    t = std::min(std::max(t, 0.0), T);
    const double f = t / dt;
    size_t i = (size_t)f;
    if (i + 1 >= r.size()) i = r.size() - 2;
    const double u = f - i;
    *ro = r[i] * (1.0 - u) + r[i + 1] * u;
    *po = phi[i] * (1.0 - u) + phi[i + 1] * u;
}

PhotonPath photon_path_equatorial(double a, double L, double r_start, double phi_start)
{
    PhotonPath p;
    bh::Ray<double> y;
    if (!bh::init_ray_constants<double>(a, r_start, 0.0, L, 0.0, 1.0, y)) return p;
    y.phi = phi_start;
    y.vu = -y.vu;                       // forward-in-time photon: integrate the (backward-parameter) ODE with negative steps
    const double u_cap = 1.0 / (r_horizon(a) + 0.01);
    p.r.push_back(r_start);
    p.phi.push_back(phi_start);
    for (int i = 0; i < 100000; ++i) {
        const double h = -bh::step_size(0.02, y);
        bh::step_ray(a, y, h);
        p.r.push_back(1.0 / y.u);
        p.phi.push_back(y.phi);
        if (y.u > u_cap) { p.captured = true; break; }
        if (y.u < 1.0 / r_start && y.vu > 0.0) break;   // outgoing again
    }
    return p;
}

Track circular_track(double a, double r, double phi0, double t_max)
{
    Track tr;
    const Circ c = circular(r, a);
    const int n = (int)(t_max / tr.dt) + 2;
    tr.r.assign(n, r);
    tr.phi.resize(n);
    for (int i = 0; i < n; ++i) tr.phi[i] = phi0 + c.omega * tr.dt * i;
    return tr;
}

Track integrate_track(double a, double E, double L, double r_start, bool inward, double phi0, double t_max)
{
    struct S { double r, vr, phi, t; };
    auto deriv = [&](const S& s, S& d) {
        const double r = s.r, X = r * r + a * a, D = r * r - 2.0 * r + a * a;
        const double P = E * X - a * L;
        const double q = r * r + (L - a * E) * (L - a * E);
        d.r = s.vr;
        d.vr = 2.0 * E * r * P - (r - 1.0) * q - r * D;
        d.phi = a * P / D - a * E + L;
        d.t = X * P / D + a * (L - a * E);
    };
    const double rp = r_horizon(a);
    S s{r_start, 0.0, phi0, 0.0};
    s.vr = (inward ? -1.0 : 1.0) * std::sqrt(std::max(Rt(a, r_start, E, L), 0.0));
    std::vector<double> T{0.0}, Rr{s.r}, Ph{s.phi};
    Track tr;
    for (int it = 0; it < 4000000; ++it) {
        S d;
        deriv(s, d);
        double h = std::min({0.01 / (std::fabs(d.phi) + 1e-9), 0.01 * s.r / (std::fabs(s.vr) + 1e-9), 0.05});
        auto ax = [&](const S& y, const S& k, double f) { return S{y.r + f * k.r, y.vr + f * k.vr, y.phi + f * k.phi, y.t + f * k.t}; };
        S k1 = d, k2, k3, k4;
        deriv(ax(s, k1, 0.5 * h), k2);
        deriv(ax(s, k2, 0.5 * h), k3);
        deriv(ax(s, k3, h), k4);
        s.r += h / 6.0 * (k1.r + 2 * (k2.r + k3.r) + k4.r);
        s.vr += h / 6.0 * (k1.vr + 2 * (k2.vr + k3.vr) + k4.vr);
        s.phi += h / 6.0 * (k1.phi + 2 * (k2.phi + k3.phi) + k4.phi);
        s.t += h / 6.0 * (k1.t + 2 * (k2.t + k3.t) + k4.t);
        T.push_back(s.t); Rr.push_back(s.r); Ph.push_back(s.phi);
        if (s.r < rp + 0.03) { tr.plunged = true; break; }
        if (s.t >= t_max || s.r > 1e3) break;
    }
    // resample uniformly in coordinate time
    const int n = (int)(T.back() / tr.dt) + 2;
    tr.r.resize(n); tr.phi.resize(n);
    size_t j = 0;
    for (int i = 0; i < n; ++i) {
        const double t = std::min(i * tr.dt, T.back());
        while (j + 2 < T.size() && T[j + 1] < t) ++j;
        const double u = (T[j + 1] > T[j]) ? (t - T[j]) / (T[j + 1] - T[j]) : 0.0;
        tr.r[i] = Rr[j] * (1.0 - u) + Rr[j + 1] * u;
        tr.phi[i] = Ph[j] * (1.0 - u) + Ph[j + 1] * u;
    }
    return tr;
}

}  // namespace phys
