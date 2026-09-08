// physics.hpp — CPU-side (double precision) Kerr black hole physics:
// horizons, ISCO, photon orbits, Novikov–Thorne / Page–Thorne disk flux,
// equatorial embedding diagram, Planck-spectrum colorimetry.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace bh {

constexpr double PI = 3.14159265358979323846;

// Outer horizon radius r_+ = M + sqrt(M² − a²), M = 1.
inline double horizon(double a) { return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a)); }

// Prograde (+φ) circular photon orbit radius for signed spin a (Bardeen 1973):
// r_ph = 2 [1 + cos(⅔ arccos(−a))]. Retrograde is r_ph(−a).
inline double photonOrbit(double a) { return 2.0 * (1.0 + std::cos(2.0 / 3.0 * std::acos(-a))); }

// Innermost stable circular orbit for a +φ orbit around a hole with signed spin a
// (Bardeen, Press & Teukolsky 1972). a < 0 therefore means a retrograde disk.
inline double isco(double a) {
    const double a2 = a * a;
    const double z1 = 1.0 + std::cbrt(1.0 - a2) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    const double z2 = std::sqrt(3.0 * a2 + z1 * z1);
    const double sgn = (a >= 0.0) ? 1.0 : -1.0;
    return 3.0 + z2 - sgn * std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
}

// Critical impact parameter ξ = L/E of the spherical photon orbit at radius r
// (Bardeen 1973). Used only for verification of the shadow.
inline double bardeenXi(double r, double a) {
    const double D = r * r - 2.0 * r + a * a;
    return ((r * r - a * a) - r * D) / (a * (r - 1.0));
}
inline double bardeenEta(double r, double a) {
    const double D = r * r - 2.0 * r + a * a;
    return r * r * r * (4.0 * D - r * (r - 1.0) * (r - 1.0)) / (a * a * (r - 1.0) * (r - 1.0));
}

// Page & Thorne (1974) eq. 15n: radiative flux of a relativistic thin disk
// (zero torque at the ISCO), in units of Ṁ/(M²).  x = sqrt(r/M).
inline double pageThorneFlux(double r, double a) {
    if (std::fabs(a) < 1e-4) a = (a < 0 ? -1e-4 : 1e-4);   // formula has a removable 0/0 at a = 0
    const double x = std::sqrt(r);
    const double x0 = std::sqrt(isco(a));
    if (x <= x0) return 0.0;
    const double th = std::acos(a) / 3.0;
    const double x1 = 2.0 * std::cos(th - PI / 3.0);
    const double x2 = 2.0 * std::cos(th + PI / 3.0);
    const double x3 = -2.0 * std::cos(th);
    auto term = [&](double xi, double xj, double xk) {
        return 3.0 * (xi - a) * (xi - a) / (xi * (xi - xj) * (xi - xk)) * std::log((x - xi) / (x0 - xi));
    };
    const double bracket = x - x0 - 1.5 * a * std::log(x / x0)
                         - term(x1, x2, x3) - term(x2, x1, x3) - term(x3, x1, x2);
    return 3.0 / (8.0 * PI) * bracket / (x * x * x * x * (x * x * x - 3.0 * x + 2.0 * a));
}

// Dimensionless Novikov–Thorne temperature profile τ(r) = (8π F(r) / 3)^{1/4}, tabulated on
// n samples between rIsco and rOut. The physical temperature is T(r) = T₀ τ(r) with
//     T₀ = [3 c⁶ Ṁ / (8π σ G² M²)]^{1/4}
// (the Newtonian profile T₀ r^{-3/4} is recovered far out, r in units of GM/c²).
inline std::vector<float> diskTemperatureProfile(double a, double rIsco, double rOut, int n, double *peakOut = nullptr) {
    std::vector<float> out(n);
    double peak = 0.0;
    for (int i = 0; i < n; ++i) {
        const double r = rIsco + (rOut - rIsco) * (i + 0.5) / n;
        const double F = std::max(0.0, pageThorneFlux(r, a));
        const double tau = std::pow(8.0 * PI * F / 3.0, 0.25);
        out[i] = (float)tau; peak = std::max(peak, tau);
    }
    if (peakOut) *peakOut = peak;
    return out;
}

// Specific energy of the circular +φ orbit at radius r and the radiative efficiency η = 1 − E_isco.
inline double circularEnergy(double r, double a) {
    const double sr = std::sqrt(r);
    return (r * r - 2.0 * r + a * sr) / (r * std::sqrt(r * r - 3.0 * r + 2.0 * a * sr));
}
inline double efficiency(double a) { return 1.0 - circularEnergy(isco(a), a); }

// Accretion rate in Eddington units implied by T₀ for a hole of mass mSolar (cgs constants):
//   T₀⁴ = 3 c⁶ Ṁ / (8π σ G² M²),  Ṁ_Edd = L_Edd / (η c²),  L_Edd = 1.26e38 (M/M☉) erg/s.
inline double eddingtonRatio(double T0, double mSolar, double a) {
    const double c = 2.99792458e10, G = 6.674e-8, sigma = 5.670374e-5, Msun = 1.989e33;
    const double M = mSolar * Msun;
    const double mdot = std::pow(T0, 4) * 8.0 * PI * sigma * G * G * M * M / (3.0 * std::pow(c, 6));
    const double mdotEdd = 1.26e38 * mSolar / (efficiency(a) * c * c);
    return mdot / mdotEdd;
}

// Equatorial embedding diagram. The induced metric on the t = const, θ = π/2
// slice of Kerr is  ds² = (r²/Δ) dr² + (r² + a² + 2a²/r) dφ².  Embedding it as a
// surface of revolution (R(r), Z(r)) in Euclidean 3-space requires
//     R(r)² = r² + a² + 2a²/r,   (dZ/dr)² = r²/Δ − (dR/dr)².
// For a = 0 this integrates to Flamm's paraboloid Z = 2 sqrt(2 (r − 2)).
// Returns samples (r, R, Z) with Z(rOut) = 0 decreasing inward, stopping where
// the embedding ceases to exist (radicand < 0) or at the horizon.
struct EmbedSample { double r, R, Z; };
inline std::vector<EmbedSample> embedding(double a, double rOut, int n) {
    const double rh = horizon(a);
    auto Rof = [&](double r) { return std::sqrt(r * r + a * a + 2.0 * a * a / r); };
    auto dZdr2 = [&](double r) {
        const double D = r * r - 2.0 * r + a * a;
        const double dR = (2.0 * r - 2.0 * a * a / (r * r)) / (2.0 * Rof(r));
        return r * r / D - dR * dR;
    };
    // Integrate from rOut down to rh with dense sampling near the horizon.
    std::vector<EmbedSample> out;
    const int m = 20000;
    double Z = 0.0;
    double rPrev = rOut;
    out.push_back({rOut, Rof(rOut), 0.0});
    for (int i = 1; i <= m; ++i) {
        const double u = (double)i / m;
        const double r = rh + (rOut - rh) * (1.0 - u) * (1.0 - u);   // quadratic clustering at rh
        if (r <= rh * (1.0 + 1e-9)) break;
        const double q0 = dZdr2(rPrev), q1 = dZdr2(r), qm = dZdr2(0.5 * (rPrev + r));
        if (q0 < 0 || q1 < 0 || qm < 0) break;
        Z -= (rPrev - r) * (std::sqrt(q0) + 4.0 * std::sqrt(qm) + std::sqrt(q1)) / 6.0;   // Simpson
        out.push_back({r, Rof(r), Z});
        rPrev = r;
    }
    // Resample to n points, evenly in sqrt(r − rmin) for a smooth throat.
    std::vector<EmbedSample> res;
    const double rmin = out.back().r;
    for (int i = 0; i < n; ++i) {
        const double u = (double)i / (n - 1);
        const double r = rmin + (rOut - rmin) * u * u;
        // linear interpolation on the dense table (it is sorted descending in r)
        auto it = std::lower_bound(out.begin(), out.end(), r, [](const EmbedSample &s, double v) { return s.r > v; });
        if (it == out.begin()) { res.push_back(out.front()); continue; }
        if (it == out.end()) { res.push_back(out.back()); continue; }
        const EmbedSample &hi = *(it - 1), &lo = *it;
        const double w = (hi.r - lo.r) > 0 ? (r - lo.r) / (hi.r - lo.r) : 0.0;
        res.push_back({r, lo.R + w * (hi.R - lo.R), lo.Z + w * (hi.Z - lo.Z)});
    }
    return res;
}

// CIE 1931 2° colour matching functions, multi-lobe Gaussian fit of
// Wyman, Sloan & Shirley (2013), λ in nanometres.
inline double cieX(double l) {
    auto g = [](double x, double mu, double s1, double s2) { const double t = (x - mu) / (x < mu ? s1 : s2); return std::exp(-0.5 * t * t); };
    return 1.056 * g(l, 599.8, 37.9, 31.0) + 0.362 * g(l, 442.0, 16.0, 26.7) - 0.065 * g(l, 501.1, 20.4, 26.2);
}
inline double cieY(double l) {
    auto g = [](double x, double mu, double s1, double s2) { const double t = (x - mu) / (x < mu ? s1 : s2); return std::exp(-0.5 * t * t); };
    return 0.821 * g(l, 568.8, 46.9, 40.5) + 0.286 * g(l, 530.9, 16.3, 31.1);
}
inline double cieZ(double l) {
    auto g = [](double x, double mu, double s1, double s2) { const double t = (x - mu) / (x < mu ? s1 : s2); return std::exp(-0.5 * t * t); };
    return 1.217 * g(l, 437.0, 11.8, 36.0) + 0.681 * g(l, 459.0, 26.0, 13.8);
}

// Planck spectral radiance B_λ(λ, T), SI (W · sr⁻¹ · m⁻³), λ in metres.
inline double planck(double lambda, double T) {
    const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
    const double x = h * c / (lambda * k * T);
    if (x > 700.0) return 0.0;
    return 2.0 * h * c * c / std::pow(lambda, 5) / std::expm1(x);
}

// Linear-sRGB radiance (relative units) of a blackbody, tabulated over
// log10 T ∈ [logTmin, logTmax]. Normalised so that T = 6500 K has luminance 1.
struct BlackbodyLUT { std::vector<float> rgb; double logTmin, logTmax; int n; };
inline BlackbodyLUT blackbodyLUT(int n, double logTmin = 2.5, double logTmax = 7.0) {
    BlackbodyLUT lut; lut.n = n; lut.logTmin = logTmin; lut.logTmax = logTmax; lut.rgb.resize(4 * n);
    auto xyz = [&](double T, double &X, double &Y, double &Z) {
        X = Y = Z = 0.0;
        for (double l = 380.0; l <= 780.0; l += 1.0) {
            const double B = planck(l * 1e-9, T);
            X += B * cieX(l); Y += B * cieY(l); Z += B * cieZ(l);
        }
    };
    double Xn, Yn, Zn; xyz(6500.0, Xn, Yn, Zn);
    for (int i = 0; i < n; ++i) {
        const double T = std::pow(10.0, logTmin + (logTmax - logTmin) * i / (n - 1));
        double X, Y, Z; xyz(T, X, Y, Z);
        X /= Yn; Y /= Yn; Z /= Yn;
        // XYZ → linear sRGB (D65)
        double r =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
        double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
        double b =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;
        // simple gamut clip preserving luminance ordering
        r = std::max(0.0, r); g = std::max(0.0, g); b = std::max(0.0, b);
        lut.rgb[4 * i + 0] = (float)r; lut.rgb[4 * i + 1] = (float)g; lut.rgb[4 * i + 2] = (float)b; lut.rgb[4 * i + 3] = (float)Y;
    }
    return lut;
}

} // namespace bh
