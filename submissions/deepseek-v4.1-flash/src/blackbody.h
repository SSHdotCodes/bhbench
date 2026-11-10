// Blackbody radiance -> linear sRGB, using Planck's law integrated against an
// analytic fit to the CIE 1931 2-degree colour matching functions
// (Wyman, Sloan & Shirley, JCGT 2013, "Simple Analytic Approximations to the
// CIE XYZ Color Matching Functions").
//
// The LUT stores *absolute* visible-band radiance, so a 30 000 K blackbody is
// genuinely brighter in the visible than a 3 000 K one. That is what makes the
// Doppler-boosted approaching side of the disk read as brighter, not just
// bluer.
#pragma once

#include <vector>

namespace bb {

struct RGB {
    double r = 0, g = 0, b = 0;
};

// CIE 1931 colour matching functions (analytic multi-lobe fit), wavelength in nm.
double cieX(double nm);
double cieY(double nm);
double cieZ(double nm);

// Planck spectral radiance B_lambda(T) in W m^-2 sr^-1 m^-1, lambda in metres.
double planckRadiance(double lambdaMeters, double T);

// Visible-band (360-830 nm) radiance of a blackbody, converted to linear sRGB.
// Units are W m^-2 sr^-1, scaled by kLutScale so typical values are O(1).
RGB blackbodyLinearRGB(double T);

// Builds a log-spaced LUT of `n` entries covering [tMin, tMax] Kelvin.
// Layout: RGB triples, row-major, ready for a GL_RGB16F 1D texture.
std::vector<float> buildLUT(int n, double tMin, double tMax);

// Reference luminance used to normalise the LUT (Y at 10 000 K).
double referenceLuminance();

constexpr double kLutMinT = 700.0;
constexpr double kLutMaxT = 250000.0;

}  // namespace bb
