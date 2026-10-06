#include "colorimetry.h"

#include <algorithm>
#include <cmath>

namespace color {

static double lobe(double x, double mu, double s1, double s2) {
    double t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}

void cie1931(double l, double& xb, double& yb, double& zb) {
    xb = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);
    yb = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);
    zb = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);
}

double planckLambda(double lam, double T) {
    const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
    double x = h * c / (lam * k * T);
    if (x > 700) return 0.0;
    return 2.0 * h * c * c / std::pow(lam, 5) / std::expm1(x);
}

void blackbodyXYZPerBolometric(double T, double& X, double& Y, double& Z) {
    const double sigma = 5.670374419e-8, pi = 3.14159265358979323846;
    X = Y = Z = 0;
    for (int i = 0; i <= 470; ++i) {   // 360..830 nm, 1 nm, trapezoid
        double l = 360.0 + i;
        double w = (i == 0 || i == 470) ? 0.5 : 1.0;
        double xb, yb, zb;
        cie1931(l, xb, yb, zb);
        double B = planckLambda(l * 1e-9, T) * 1e-9 * w;   // per nm
        X += B * xb;
        Y += B * yb;
        Z += B * zb;
    }
    double bol = sigma * T * T * T * T / pi;
    X /= bol;
    Y /= bol;
    Z /= bol;
}

simd_double3 xyzToLinearSRGB(double X, double Y, double Z) {
    return simd_make_double3(3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,
                             -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
                             0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
}

std::vector<simd_float4> buildBlackbodyLUT(int n, double logT0, double logT1) {
    std::vector<simd_float4> lut(n);
    for (int i = 0; i < n; ++i) {
        double T = std::pow(10.0, logT0 + (logT1 - logT0) * i / (n - 1));
        double X, Y, Z;
        blackbodyXYZPerBolometric(T, X, Y, Z);
        simd_double3 rgb = xyzToLinearSRGB(X, Y, Z);
        // Out-of-gamut (very red blackbodies): move towards the grey of equal luminance until inside.
        double mn = std::min({rgb.x, rgb.y, rgb.z});
        if (mn < 0 && Y > 0) {
            double s = Y / (Y - mn);
            rgb = simd_make_double3(Y + (rgb.x - Y) * s, Y + (rgb.y - Y) * s, Y + (rgb.z - Y) * s);
        }
        lut[i] = simd_make_float4(float(std::max(rgb.x, 0.0)), float(std::max(rgb.y, 0.0)), float(std::max(rgb.z, 0.0)),
                                  float(Y));
    }
    return lut;
}

}  // namespace color
