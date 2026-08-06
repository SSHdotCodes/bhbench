// spectrum.hpp — physical constants, blackbody radiometry, and the conversion
// from a Planck spectrum to linear sRGB via the CIE 1931 observer.

#pragma once
#include <vector>
#include <array>

namespace spectrum {

// ---- physical constants (CGS) ----
constexpr double C_LIGHT   = 2.99792458e10;    // cm/s
constexpr double G_NEWTON  = 6.67430e-8;       // cm^3 g^-1 s^-2
constexpr double H_PLANCK  = 6.62607015e-27;   // erg s
constexpr double K_BOLTZ   = 1.380649e-16;     // erg/K
constexpr double SIGMA_SB  = 5.670374419e-5;   // erg cm^-2 s^-1 K^-4
constexpr double M_SUN     = 1.98892e33;       // g
constexpr double M_PROTON  = 1.67262192e-24;   // g
constexpr double SIGMA_T   = 6.6524587e-25;    // cm^2  (Thomson cross section)
constexpr double YEAR      = 3.15576e7;        // s

// Gravitational radius r_g = GM/c^2 in cm.
double gravRadius(double massSolar);

// Eddington luminosity in erg/s.
double eddingtonLuminosity(double massSolar);

// Accretion rate in g/s that produces `eddingtonRatio` x L_Edd given a
// radiative efficiency eta.
double accretionRate(double massSolar, double eddingtonRatio, double eta);

// Convert the dimensionless Novikov-Thorne flux (G=c=M=Mdot=1) at a given
// radius into an effective blackbody temperature in Kelvin.
double effectiveTemperature(double fluxDimensionless, double massSolar, double mdotCGS);

// Peak (Wien) wavelength in nm.
double wienPeakNm(double tempK);

// ---- colour ----

// Chromaticity of a Planck spectrum at temperature T, as linear sRGB
// normalised so that the luminance Y = 1.  Out-of-gamut colours are
// desaturated toward white rather than clipped.
std::array<float, 3> blackbodyRGB(double tempK);

// Build a 1-D lookup table of blackbody chromaticities.  The index maps
// linearly onto log10(T) between logTmin and logTmax.
std::vector<float> blackbodyLUT(int n, double logTmin, double logTmax);

// CIE 1931 colour matching functions (multi-lobe Gaussian fit,
// Wyman, Sloan & Shirley 2013 — max error < 1%).
void cie1931(double lambdaNm, double& x, double& y, double& z);

}  // namespace spectrum
