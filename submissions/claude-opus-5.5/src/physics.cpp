// physics.cpp — Kerr circular orbits, Novikov–Thorne flux, temperature scale and embedding diagram.
#include "physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace phys {

double horizon(double a) { return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a)); }

double isco(double a) {
    double a2 = a * a;
    double z1 = 1.0 + std::cbrt(1.0 - a2) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double z2 = std::sqrt(3.0 * a2 + z1 * z1);
    double s = a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0);
    return 3.0 + z2 - s * std::sqrt(std::max(0.0, (3.0 - z1) * (3.0 + z1 + 2.0 * z2)));
}

double photonOrbit(double a, int prograde) {
    return 2.0 * (1.0 + std::cos(2.0 / 3.0 * std::acos(-prograde * std::fabs(a))));
}

// Bardeen, Press & Teukolsky (1972), upper sign with signed a.
static double circDen(double a, double r) {
    double x = std::sqrt(r);
    return std::pow(r, 0.75) * std::sqrt(r * x - 3.0 * x + 2.0 * a);
}
double circE(double a, double r) {
    double x = std::sqrt(r);
    return (r * x - 2.0 * x + a) / circDen(a, r);
}
double circL(double a, double r) {
    double x = std::sqrt(r);
    return (r * r - 2.0 * a * x + a * a) / circDen(a, r);
}
double circOmega(double a, double r) { return 1.0 / (r * std::sqrt(r) + a); }

double equatorialCircumferentialRadius(double a, double r) { return std::sqrt(r * r + a * a + 2.0 * a * a / r); }

static double dLdr(double a, double r) {
    double h = 1e-5 * r;
    return (circL(a, r + h) - circL(a, r - h)) / (2.0 * h);
}
static double dOmegadr(double a, double r) {
    double x = std::sqrt(r);
    return -1.5 * x / ((r * x + a) * (r * x + a));
}
static double ntIntegrand(double a, double r) { return (circE(a, r) - circOmega(a, r) * circL(a, r)) * dLdr(a, r); }
static double ntPrefactor(double a, double r) {
    double eol = circE(a, r) - circOmega(a, r) * circL(a, r);
    return -dOmegadr(a, r) / (4.0 * PI * r * eol * eol);
}

double ntFluxDirect(double a, double r) {
    double rin = isco(a);
    if (r <= rin) return 0.0;
    // Gauss–Legendre on r = rin + (r − rin) u² (the integrand is smooth; the map clusters nodes at the ISCO)
    static const double xg[8] = {-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                 0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363};
    static const double wg[8] = {0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                 0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};
    const int panels = 64;
    double sum = 0;
    for (int p = 0; p < panels; ++p) {
        double u0 = double(p) / panels, u1 = double(p + 1) / panels;
        for (int j = 0; j < 8; ++j) {
            double u = 0.5 * (u0 + u1) + 0.5 * (u1 - u0) * xg[j];
            double rr = rin + (r - rin) * u * u;
            sum += 0.5 * (u1 - u0) * wg[j] * ntIntegrand(a, rr) * 2.0 * (r - rin) * u;
        }
    }
    return ntPrefactor(a, r) * sum;
}

// a = 0 closed form (derived by hand): E − ΩL = sqrt(1 − 3/r), L' = ½ r^{−1/2}(1 − 6/r)(1 − 3/r)^{−3/2}, so
// ∫(E − ΩL)L' dr = [u − (√3/2) ln((u − √3)/(u + √3))] from √6 to √r, and
// F̃ = 3/(8π) · 1/(r^{5/2}(r − 3)) · [√r − √6 − (√3/2) ln( (√r − √3)(√6 + √3) / ((√r + √3)(√6 − √3)) )].
double ntFluxSchwarzschild(double r) {
    if (r <= 6.0) return 0.0;
    double s3 = std::sqrt(3.0), s6 = std::sqrt(6.0), x = std::sqrt(r);
    double br = x - s6 - 0.5 * s3 * std::log(((x - s3) * (s6 + s3)) / ((x + s3) * (s6 - s3)));
    return 3.0 / (8.0 * PI) / (std::pow(r, 2.5) * (r - 3.0)) * br;
}

double radiativeEfficiency(double a) { return 1.0 - circE(a, isco(a)); }

DiskModel buildDisk(double a, double rout, int samples) {
    DiskModel d;
    d.a = a;
    d.rin = isco(a);
    d.rout = std::max(rout, d.rin + 0.5);
    // Fine cumulative integral on the same quadratic map, then resample.
    const int fine = 16384;
    std::vector<double> rr(fine + 1), cum(fine + 1, 0.0);
    for (int i = 0; i <= fine; ++i) {
        double u = double(i) / fine;
        rr[i] = d.rin + (d.rout - d.rin) * u * u;
    }
    // Simpson per interval using the midpoint.
    for (int i = 1; i <= fine; ++i) {
        double r0 = rr[i - 1], r1 = rr[i], rm = 0.5 * (r0 + r1);
        double f0 = i == 1 ? 0.0 : ntIntegrand(a, r0);   // L' = 0 exactly at the ISCO
        cum[i] = cum[i - 1] + (r1 - r0) / 6.0 * (f0 + 4.0 * ntIntegrand(a, rm) + ntIntegrand(a, r1));
    }
    std::vector<double> F(fine + 1, 0.0);
    d.fluxMax = 0;
    for (int i = 1; i <= fine; ++i) {
        F[i] = ntPrefactor(a, rr[i]) * cum[i];
        if (F[i] > d.fluxMax) { d.fluxMax = F[i]; d.rPeak = rr[i]; }
    }
    d.flux.resize(samples);
    for (int i = 0; i < samples; ++i) {
        double u = double(i) / (samples - 1);
        double x = u * fine;   // same map ⇒ index is linear in u
        int j = std::min(int(x), fine - 1);
        double f = x - j;
        d.flux[i] = float(((1.0 - f) * F[j] + f * F[j + 1]) / d.fluxMax);
    }
    return d;
}

double diskLuminosityIntegral(double a, double rmax) {
    // ∫_{r_ms}^{rmax} 4π r E(r) F̃(r) dr on a logarithmic grid (both faces: 2 × 2π r).
    double rin = isco(a);
    const int n = 200000;
    double sum = 0;
    double lr0 = std::log(rin), lr1 = std::log(rmax);
    // cumulative integral for F̃ computed on the fly
    double cum = 0, rprev = rin, gprev = 0;
    double hprev = 0;
    for (int i = 1; i <= n; ++i) {
        double r = std::exp(lr0 + (lr1 - lr0) * double(i) / n);
        double g = ntIntegrand(a, r);
        double rm = 0.5 * (r + rprev);
        cum += (r - rprev) / 6.0 * (gprev + 4.0 * ntIntegrand(a, rm) + g);
        double F = ntPrefactor(a, r) * cum;
        double h = 4.0 * PI * r * circE(a, r) * F;
        sum += 0.5 * (h + hprev) * (r - rprev);
        hprev = h;
        gprev = g;
        rprev = r;
    }
    return sum;
}

double eddingtonLuminosity(double massSolar) {
    return 4.0 * PI * G * massSolar * M_SUN * M_PROTON * C / SIGMA_THOMSON;
}

double gravitationalTimeSeconds(double massSolar) { return G * massSolar * M_SUN / (C * C * C); }

static double fluxScale(double massSolar, double mdotKgS) {   // Ṁ c⁶ / (G² M²)  [W m⁻²]
    double M = massSolar * M_SUN;
    return mdotKgS * std::pow(C, 6) / (G * G * M * M);
}

double peakTemperature(double a, double massSolar, double mdotEdd, double fluxMax) {
    double eta = radiativeEfficiency(a);
    double mdot = mdotEdd * eddingtonLuminosity(massSolar) / (eta * C * C);
    return std::pow(fluxScale(massSolar, mdot) * fluxMax / SIGMA_SB, 0.25);
}

double mdotForPeakTemperature(double a, double massSolar, double Tpeak, double fluxMax) {
    double eta = radiativeEfficiency(a);
    double mdotEddKg = eddingtonLuminosity(massSolar) / (eta * C * C);
    double F = SIGMA_SB * std::pow(Tpeak, 4);
    double mdot = F / (fluxScale(massSolar, 1.0) * fluxMax);
    return mdot / mdotEddKg;
}

// ---- embedding ----
static double embedSlope(double a, double r) {   // dz/dr (clamped at 0 where not embeddable)
    double delta = r * r - 2.0 * r + a * a;
    double R = equatorialCircumferentialRadius(a, r);
    double dR = (r - a * a / (r * r)) / R;
    return std::sqrt(std::max(0.0, r * r / delta - dR * dR));
}

double embeddingHeight(double a, double r) {
    // r = r+ + u², dz = z'(r) 2u du (integrable 1/sqrt(r − r+) singularity removed)
    double rp = horizon(a);
    if (r <= rp) return 0.0;
    double U = std::sqrt(r - rp);
    const int n = 4000;
    double sum = 0;
    for (int i = 0; i < n; ++i) {   // midpoint rule in u (integrand finite at u = 0)
        double u = (i + 0.5) * U / n;
        sum += embedSlope(a, rp + u * u) * 2.0 * u;
    }
    return sum * U / n;
}

Funnel buildFunnel(double a, double R1, double top, double scale, int samples) {
    Funnel f;
    double rp = horizon(a);
    f.R0 = equatorialCircumferentialRadius(a, rp);
    f.R1 = R1;
    // tabulate (R(r), z(r)) on a fine u-grid, then resample on the R grid
    double rmax = R1;   // R(r) ≥ r, so r = R1 is beyond the outer edge
    while (equatorialCircumferentialRadius(a, rmax) < R1) rmax *= 1.1;
    double U = std::sqrt(rmax - rp);
    const int n = 20000;
    std::vector<double> Rv(n + 1), zv(n + 1);
    Rv[0] = f.R0;
    zv[0] = 0;
    double z = 0;
    for (int i = 1; i <= n; ++i) {
        double u0 = (i - 1) * U / n, u1 = i * U / n, um = 0.5 * (u0 + u1);
        double sl = embedSlope(a, rp + um * um);
        if (sl == 0.0) f.nonEmbeddable++;
        z += sl * 2.0 * um * (u1 - u0);
        Rv[i] = equatorialCircumferentialRadius(a, rp + u1 * u1);
        zv[i] = z;
    }
    auto zOfR = [&](double R) {
        auto it = std::lower_bound(Rv.begin(), Rv.end(), R);
        if (it == Rv.begin()) return zv[0];
        if (it == Rv.end()) return zv[n];
        size_t j = size_t(it - Rv.begin());
        double t = (R - Rv[j - 1]) / (Rv[j] - Rv[j - 1]);
        return zv[j - 1] + t * (zv[j] - zv[j - 1]);
    };
    double zTop = zOfR(R1);
    f.depth = zTop;
    f.Z.resize(samples);
    for (int i = 0; i < samples; ++i) {
        double u = double(i) / (samples - 1);
        double R = f.R0 + (f.R1 - f.R0) * u * u;
        f.Z[i] = float(top - scale * (zTop - zOfR(R)));
    }
    return f;
}

std::string formatSI(double v, int digits) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", digits, v);
    return buf;
}

}  // namespace phys
