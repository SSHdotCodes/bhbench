// =============================================================================
//  blackbody.hpp — colour of a Planck radiator as seen on an sRGB display.
//
//  Integrates Planck's law against the CIE 1931 2-degree colour-matching
//  functions (analytic multi-lobe fit of Wyman, Sloan & Shirley 2013) and
//  converts XYZ -> linear sRGB (D65).  Because B_nu / nu^3 is a Lorentz
//  invariant, a blackbody at temperature T seen with redshift factor g is
//  exactly a blackbody at g*T, so one table in T covers Doppler boosting and
//  gravitational redshift with no approximation.
// =============================================================================
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace blackbody {

inline double lobe(double x, double mu, double s1, double s2) {
    double t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}
inline void cie1931(double nm, double& X, double& Y, double& Z) {
    X = 1.056 * lobe(nm, 599.8, 37.9, 31.0) + 0.362 * lobe(nm, 442.0, 16.0, 26.7) -
        0.065 * lobe(nm, 501.1, 20.4, 26.2);
    Y = 0.821 * lobe(nm, 568.8, 46.9, 40.5) + 0.286 * lobe(nm, 530.9, 16.3, 31.1);
    Z = 1.217 * lobe(nm, 437.0, 11.8, 36.0) + 0.681 * lobe(nm, 459.0, 26.0, 13.8);
}

// Spectral radiance B_lambda(T), arbitrary overall scale.
inline double planck(double nm, double T) {
    const double c2 = 1.438776877e7;             // h c / k_B  in nm K
    double x = c2 / (nm * T);
    double l = nm * 1e-3;                        // micrometres keeps numbers tame
    return 1.0 / (l * l * l * l * l * std::expm1(x));
}

inline void xyzOf(double T, double& X, double& Y, double& Z) {
    X = Y = Z = 0.0;
    for (double nm = 360.0; nm <= 830.0; nm += 1.0) {
        double x, y, z;
        cie1931(nm, x, y, z);
        double B = planck(nm, T);
        X += B * x; Y += B * y; Z += B * z;
    }
}

inline void xyzToLinearSRGB(double X, double Y, double Z, double rgb[3]) {
    rgb[0] =  3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    rgb[1] = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    rgb[2] =  0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
}

struct Table {
    double logTmin, logTmax;
    int n;
    std::vector<float> rgba;      // rgb: colour at unit luminance, a: ln(luminance)

    double logLuminance(double T) const {
        double u = (std::log(T) - logTmin) / (logTmax - logTmin) * (n - 1);
        u = std::clamp(u, 0.0, double(n - 1) - 1e-9);
        int i = int(u);
        double t = u - i;
        return rgba[4 * i + 3] * (1.0 - t) + rgba[4 * (i + 1) + 3] * t;
    }
};

inline Table build(double Tmin = 400.0, double Tmax = 1.0e6, int n = 1024) {
    Table tab;
    tab.logTmin = std::log(Tmin);
    tab.logTmax = std::log(Tmax);
    tab.n = n;
    tab.rgba.resize(4 * n);
    for (int i = 0; i < n; ++i) {
        double T = std::exp(tab.logTmin + (tab.logTmax - tab.logTmin) * i / (n - 1));
        double X, Y, Z, rgb[3];
        xyzOf(T, X, Y, Z);
        xyzToLinearSRGB(X, Y, Z, rgb);
        // Out-of-gamut (very red) colours: clip negative lobes, keep luminance.
        for (double& c : rgb) c = std::max(c, 0.0);
        double lum = 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2];
        double inv = lum > 0 ? 1.0 / lum : 0.0;
        tab.rgba[4 * i + 0] = float(rgb[0] * inv);
        tab.rgba[4 * i + 1] = float(rgb[1] * inv);
        tab.rgba[4 * i + 2] = float(rgb[2] * inv);
        tab.rgba[4 * i + 3] = float(std::log(std::max(Y, 1e-300)));
    }
    return tab;
}

}  // namespace blackbody
