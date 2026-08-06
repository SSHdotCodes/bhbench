#include "spectrum.hpp"
#include <cmath>
#include <algorithm>

namespace spectrum {

double gravRadius(double massSolar) {
    return G_NEWTON * massSolar * M_SUN / (C_LIGHT * C_LIGHT);
}

double eddingtonLuminosity(double massSolar) {
    return 4.0 * M_PI * G_NEWTON * massSolar * M_SUN * M_PROTON * C_LIGHT / SIGMA_T;
}

double accretionRate(double massSolar, double eddingtonRatio, double eta) {
    eta = std::max(1e-3, eta);
    return eddingtonRatio * eddingtonLuminosity(massSolar) / (eta * C_LIGHT * C_LIGHT);
}

double effectiveTemperature(double fluxDimensionless, double massSolar, double mdotCGS) {
    if (fluxDimensionless <= 0.0) return 0.0;
    double rg = gravRadius(massSolar);
    // F_phys = F_dimensionless * Mdot c^2 / r_g^2
    double F = fluxDimensionless * mdotCGS * C_LIGHT * C_LIGHT / (rg * rg);
    return std::pow(F / SIGMA_SB, 0.25);
}

double wienPeakNm(double tempK) {
    if (tempK <= 0.0) return 0.0;
    return 2.897771955e6 / tempK;   // b = 2.8977719e-3 m K -> nm
}

// ---------------------------------------------------------------------------

static inline double gaussLobe(double x, double mu, double s1, double s2) {
    double t = (x - mu) * (x < mu ? 1.0 / s1 : 1.0 / s2);
    return std::exp(-0.5 * t * t);
}

void cie1931(double l, double& x, double& y, double& z) {
    x = 1.056 * gaussLobe(l, 599.8, 37.9, 31.0)
      + 0.362 * gaussLobe(l, 442.0, 16.0, 26.7)
      - 0.065 * gaussLobe(l, 501.1, 20.4, 26.2);
    y = 0.821 * gaussLobe(l, 568.8, 46.9, 40.5)
      + 0.286 * gaussLobe(l, 530.9, 16.3, 31.1);
    z = 1.217 * gaussLobe(l, 437.0, 11.8, 36.0)
      + 0.681 * gaussLobe(l, 459.0, 26.0, 13.8);
}

// Planck spectral radiance B_lambda, arbitrary normalisation (the overall scale
// cancels when we normalise the chromaticity).
static double planck(double lambdaNm, double T) {
    double l = lambdaNm * 1e-7;                    // cm
    double a = 2.0 * H_PLANCK * C_LIGHT * C_LIGHT / std::pow(l, 5);
    double e = H_PLANCK * C_LIGHT / (l * K_BOLTZ * T);
    if (e > 700.0) return 0.0;                     // avoid overflow in exp
    return a / (std::exp(e) - 1.0);
}

std::array<float, 3> blackbodyRGB(double T) {
    if (T <= 0.0) return {0.f, 0.f, 0.f};

    double X = 0, Y = 0, Z = 0;
    const double lo = 360.0, hi = 830.0, step = 1.0;
    for (double l = lo; l <= hi; l += step) {
        double xb, yb, zb;
        cie1931(l, xb, yb, zb);
        double B = planck(l, T);
        X += B * xb * step;
        Y += B * yb * step;
        Z += B * zb * step;
    }
    if (Y <= 0.0) {
        // Beyond the range where the visible band carries any energy at all.
        // The chromaticity limit of a very hot Planck source is a fixed blue;
        // return it rather than black.
        return blackbodyRGB(1.0e6);
    }
    X /= Y; Z /= Y; Y = 1.0;

    // CIE XYZ (D65) -> linear sRGB
    double r =  3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    double g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    double b =  0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;

    // Desaturate out-of-gamut colours toward white instead of clipping, which
    // keeps the hue continuous across the whole temperature range.
    double m = std::min({r, g, b});
    if (m < 0.0) { r -= m; g -= m; b -= m; }

    // Renormalise to unit luminance so brightness is carried separately by the
    // radiometry (sigma T^4), not by the colour.
    double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    if (lum > 1e-12) { r /= lum; g /= lum; b /= lum; }

    return {(float)r, (float)g, (float)b};
}

std::vector<float> blackbodyLUT(int n, double logTmin, double logTmax) {
    std::vector<float> out(n * 3);
    for (int i = 0; i < n; ++i) {
        double f = (n > 1) ? double(i) / double(n - 1) : 0.0;
        double T = std::pow(10.0, logTmin + f * (logTmax - logTmin));
        auto c = blackbodyRGB(T);
        out[i * 3 + 0] = c[0];
        out[i * 3 + 1] = c[1];
        out[i * 3 + 2] = c[2];
    }
    return out;
}

}  // namespace spectrum
