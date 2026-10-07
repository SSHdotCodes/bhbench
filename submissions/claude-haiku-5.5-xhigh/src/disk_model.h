// Host-side (double precision) model of the Novikov-Thorne thin accretion disk around a Kerr hole.
//
// Circular equatorial prograde orbits (Bardeen, Press & Teukolsky 1972), units M = 1:
//   Omega = 1 / (r^{3/2} + a)
//   E     = (r^{3/2} - 2 r^{1/2} + a) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} + 2a))
//   L     = (r^2 - 2 a r^{1/2} + a^2) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} + 2a))
//   u^t   = (r^{3/2} + a) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} + 2a))
// Page & Thorne (1974) flux, with sqrt(-g) = r on the equator of Kerr:
//   F(r) = Mdot f(r) / (4 pi r),   f(r) = -Omega'(r) / (E - Omega L)^2 * int_{r_isco}^r (E - Omega L) L' dr'
// Energy conservation requires  int_{r_isco}^inf E f dr = 1 - E_isco  (per unit Mdot); the tests check this.
#ifndef BH_DISK_MODEL_H
#define BH_DISK_MODEL_H

#include <cmath>
#include <vector>
#include <algorithm>

struct CircularOrbit {
    double E;
    double L;
    double Omega;
    double ut;
};

// Prograde innermost stable circular orbit (Bardeen 1972).
inline double isco_radius(double a) {
    double z1 = 1.0 + std::cbrt(1.0 - a * a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    return 3.0 + z2 - std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
}

// Prograde circular photon orbit: root of r^{3/2} - 3 r^{1/2} + 2a = 0 (r = 3 for a = 0).
inline double photon_orbit_prograde(double a) {
    double lo = 1.0, hi = 3.0;
    auto f = [a](double r) { return std::pow(r, 1.5) - 3.0 * std::sqrt(r) + 2.0 * a; };
    for (int i = 0; i < 200; ++i) {
        double mid = 0.5 * (lo + hi);
        if (f(mid) > 0.0) hi = mid; else lo = mid;
    }
    return 0.5 * (lo + hi);
}

inline CircularOrbit circular_orbit(double a, double r) {
    double r32 = r * std::sqrt(r);
    double sr = std::sqrt(r);
    double den = std::pow(r, 0.75) * std::sqrt(r32 - 3.0 * sr + 2.0 * a);
    CircularOrbit o;
    o.Omega = 1.0 / (r32 + a);
    o.E = (r32 - 2.0 * sr + a) / den;
    o.L = (r * r - 2.0 * a * sr + a * a) / den;
    o.ut = (r32 + a) / den;
    return o;
}

// Page-Thorne f(r) on a monotone grid that starts at r_isco. Trapezoid rule on the cumulative integral.
inline std::vector<double> nt_f_on_grid(double a, const std::vector<double>& rg) {
    size_t n = rg.size();
    std::vector<double> g(n), I(n, 0.0), f(n, 0.0);
    for (size_t k = 0; k < n; ++k) {
        double r = rg[k];
        double h = 1e-6 * r;
        double Lp = (circular_orbit(a, r + h).L - circular_orbit(a, r - h).L) / (2.0 * h);
        CircularOrbit o = circular_orbit(a, r);
        g[k] = (o.E - o.Omega * o.L) * Lp;
    }
    for (size_t k = 1; k < n; ++k) {
        I[k] = I[k - 1] + 0.5 * (g[k] + g[k - 1]) * (rg[k] - rg[k - 1]);
    }
    for (size_t k = 0; k < n; ++k) {
        double r = rg[k];
        CircularOrbit o = circular_orbit(a, r);
        double dOmega = -1.5 * std::sqrt(r) / std::pow(std::pow(r, 1.5) + a, 2.0);
        double den = o.E - o.Omega * o.L;
        f[k] = -dOmega / (den * den) * I[k];
    }
    return f;
}

struct DiskModel {
    double a = 0.0;
    double r_isco = 6.0;
    double r_out = 16.0;
    double E_isco = 0.0;
    double efficiency = 0.0;     // 1 - E_isco, radiated fraction of rest energy
    double T_max_K = 0.0;        // physical peak temperature for M = 10 M_sun, Mdot = 1e-8 M_sun/yr
    std::vector<float> lut;      // F / F_max on a uniform grid [r_isco, r_out]
};

// Builds the flux table for the renderer. n_lut is the number of table samples.
inline DiskModel build_disk(double a, double r_out, int n_lut) {
    DiskModel d;
    d.a = a;
    d.r_isco = isco_radius(a);
    d.r_out = r_out;
    d.E_isco = circular_orbit(a, d.r_isco).E;
    d.efficiency = 1.0 - d.E_isco;

    std::vector<double> rg(n_lut);
    for (int k = 0; k < n_lut; ++k) {
        rg[k] = d.r_isco + (r_out - d.r_isco) * double(k) / double(n_lut - 1);
    }
    std::vector<double> f = nt_f_on_grid(a, rg);
    std::vector<double> F(n_lut);
    double Fmax = 0.0;
    for (int k = 0; k < n_lut; ++k) {
        F[k] = f[k] / (4.0 * M_PI * rg[k]);      // per unit Mdot in M = 1 units
        Fmax = std::max(Fmax, F[k]);
    }
    d.lut.resize(n_lut);
    for (int k = 0; k < n_lut; ++k) {
        d.lut[k] = float(F[k] / Fmax);
    }
    // Physical scale: F_phys = (c^6 / (G^2 M^2)) * Mdot_phys * F_geom.
    const double c = 2.99792458e8, G = 6.67430e-11, Msun = 1.98847e30, yr = 3.15576e7, sigma = 5.670374e-8;
    double M = 10.0 * Msun;
    double Mdot = 1e-8 * Msun / yr;
    double Fphys = c * c * c * c * c * c / (G * G * M * M) * Mdot * Fmax;
    d.T_max_K = std::pow(Fphys / sigma, 0.25);
    return d;
}

#endif
