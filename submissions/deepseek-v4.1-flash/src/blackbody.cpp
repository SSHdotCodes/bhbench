#include "blackbody.h"

#include <algorithm>
#include <cmath>

namespace bb {

namespace {

constexpr double kH = 6.62607015e-34;   // J s
constexpr double kC = 2.99792458e8;     // m/s
constexpr double kKb = 1.380649e-23;    // J/K

// Piecewise Gaussian used by the Wyman et al. fit.
inline double lobe(double x, double mu, double s1, double s2) {
    double s = (x < mu) ? s1 : s2;
    double t = (x - mu) / s;
    return std::exp(-0.5 * t * t);
}

}  // namespace

double cieX(double nm) {
    return 1.056 * lobe(nm, 599.8, 37.9, 31.0) +
           0.362 * lobe(nm, 442.0, 16.0, 26.7) -
           0.065 * lobe(nm, 501.1, 20.4, 26.2);
}

double cieY(double nm) {
    return 0.821 * lobe(nm, 568.8, 46.9, 40.5) +
           0.286 * lobe(nm, 530.9, 16.3, 31.1);
}

double cieZ(double nm) {
    return 1.217 * lobe(nm, 437.0, 11.8, 36.0) +
           0.681 * lobe(nm, 459.0, 26.0, 13.8);
}

double planckRadiance(double lambdaMeters, double T) {
    if (T <= 0.0) return 0.0;
    double l5 = lambdaMeters * lambdaMeters * lambdaMeters * lambdaMeters * lambdaMeters;
    double x = kH * kC / (lambdaMeters * kKb * T);
    if (x > 700.0) return 0.0;  // Wien tail underflows
    double denom = std::expm1(x);
    return (2.0 * kH * kC * kC) / (l5 * denom);
}

namespace {

// Integrates Planck against the CMFs over the visible band.
RGB integrateVisible(double T) {
    constexpr double lo = 360.0, hi = 830.0, step = 1.0;
    double X = 0, Y = 0, Z = 0;
    for (double nm = lo; nm <= hi; nm += step) {
        double B = planckRadiance(nm * 1e-9, T);
        if (B == 0.0) continue;
        X += B * cieX(nm);
        Y += B * cieY(nm);
        Z += B * cieZ(nm);
    }
    const double dl = step * 1e-9;
    X *= dl;
    Y *= dl;
    Z *= dl;

    // XYZ -> linear sRGB (IEC 61966-2-1).
    RGB c;
    c.r = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    c.g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    c.b = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;

    // Out-of-gamut handling: desaturate toward the neutral axis rather than
    // clipping, which would shift hue.
    double mn = std::min({c.r, c.g, c.b});
    if (mn < 0.0) {
        c.r -= mn;
        c.g -= mn;
        c.b -= mn;
    }
    return c;
}

double gScale = 0.0;

double scale() {
    if (gScale == 0.0) {
        RGB ref = integrateVisible(10000.0);
        double y = 0.2126 * ref.r + 0.7152 * ref.g + 0.0722 * ref.b;
        gScale = (y > 0.0) ? 1.0 / y : 1.0;
    }
    return gScale;
}

}  // namespace

double referenceLuminance() { return 1.0 / scale(); }

RGB blackbodyLinearRGB(double T) {
    RGB c = integrateVisible(T);
    double s = scale();
    c.r *= s;
    c.g *= s;
    c.b *= s;
    return c;
}

std::vector<float> buildLUT(int n, double tMin, double tMax) {
    std::vector<float> out(static_cast<size_t>(n) * 3);
    double lmin = std::log(tMin), lmax = std::log(tMax);
    for (int i = 0; i < n; ++i) {
        double t = (n == 1) ? 0.0 : double(i) / double(n - 1);
        double T = std::exp(lmin + t * (lmax - lmin));
        RGB c = blackbodyLinearRGB(T);
        out[i * 3 + 0] = static_cast<float>(c.r);
        out[i * 3 + 1] = static_cast<float>(c.g);
        out[i * 3 + 2] = static_cast<float>(c.b);
    }
    return out;
}

}  // namespace bb
