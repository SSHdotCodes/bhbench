// colorimetry.h — blackbody colours from first principles (Planck's law × CIE 1931 2° observer → sRGB).
#pragma once
#include <simd/simd.h>
#include <vector>

namespace color {

// CIE 1931 2° colour-matching functions (multi-lobe fit of Wyman, Sloan & Shirley 2013, ~1% accuracy).
void cie1931(double lambdaNm, double& xbar, double& ybar, double& zbar);

// Planck spectral radiance B_λ(T) [W sr⁻¹ m⁻³].
double planckLambda(double lambdaMeters, double T);

// CIE XYZ of B_λ(T) (integrated 360–830 nm) divided by the bolometric radiance σT⁴/π.
void blackbodyXYZPerBolometric(double T, double& X, double& Y, double& Z);

// Linear sRGB (D65) from XYZ.
simd_double3 xyzToLinearSRGB(double X, double Y, double Z);

// LUT over log10 T: rgb = linear-sRGB per unit bolometric radiance (gamut-mapped by desaturating at
// constant luminance), w = luminance Y per unit bolometric radiance.
std::vector<simd_float4> buildBlackbodyLUT(int n, double logT0, double logT1);

}  // namespace color
