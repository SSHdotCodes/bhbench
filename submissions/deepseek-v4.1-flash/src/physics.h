// Physical model for a Schwarzschild black hole.
//
// Units: geometric, G = c = 1, and the mass is the length unit (M = 1).
// Therefore the Schwarzschild radius is r_s = 2M = 2, the photon sphere sits at
// r = 3M = 3, the ISCO at r = 6M = 6, and the critical impact parameter of the
// shadow is b_c = 3*sqrt(3) M ~= 5.19615.
#pragma once

#include <algorithm>
#include <cmath>

namespace bh {

// --- geometric-unit landmarks (M = 1) --------------------------------------
constexpr double kRs = 2.0;                 // Schwarzschild radius
constexpr double kPhotonSphere = 3.0;       // unstable circular photon orbit
constexpr double kIsco = 6.0;               // innermost stable circular orbit
constexpr double kBCrit = 5.196152422706632;  // 3*sqrt(3): shadow edge
// 1 - E_ISCO = 1 - sqrt(8/9): the fraction of rest mass radiated by a thin disk.
constexpr double kDiskEfficiency = 0.05719095841793664;

// --- SI / cgs constants ----------------------------------------------------
constexpr double kG = 6.67430e-8;        // cm^3 g^-1 s^-2
constexpr double kC = 2.99792458e10;     // cm s^-1
constexpr double kMsun = 1.98892e33;     // g
constexpr double kSigmaSB = 5.670374419e-5;  // erg cm^-2 s^-1 K^-4
constexpr double kSigmaT = 6.6524587321e-25; // cm^2
constexpr double kMp = 1.67262192e-24;   // g
constexpr double kGMc2Sun = 1.47664e5;   // cm, gravitational radius of the Sun

// Gravitational radius GM/c^2 for a mass in solar masses, in cm.
inline double gravitationalRadiusCm(double massSolar) { return kGMc2Sun * massSolar; }

// Eddington luminosity in erg/s.
inline double eddingtonLuminosity(double massSolar) {
    return 4.0 * M_PI * kG * massSolar * kMsun * kMp * kC / kSigmaT;
}

// Eddington accretion rate in g/s for a radiatively efficient thin disk.
inline double eddingtonAccretionRate(double massSolar) {
    return eddingtonLuminosity(massSolar) / (kDiskEfficiency * kC * kC);
}

// ---------------------------------------------------------------------------
// Novikov-Thorne / Shakura-Sunyaev thin disk with zero torque at the inner edge.
//
//   F(r)  = 3 G M Mdot / (8 pi r^3) * (1 - sqrt(r_in / r))
//   T(r)  = [F(r) / sigma_SB]^(1/4)
//
// Written in terms of x = r / r_in the normalised temperature profile is
//   Theta(x) = x^(-3/4) * (1 - x^(-1/2))^(1/4)
// which peaks at x = 49/36 with Theta = 0.48787...
// ---------------------------------------------------------------------------
constexpr double kNTProfilePeakX = 49.0 / 36.0;
constexpr double kNTProfilePeak = 0.487871;  // Theta(49/36)

// Normalised temperature profile, 0 at x = 1, peak 1.0 at x = 49/36.
inline double ntTheta(double x) {
    if (x <= 1.0) return 0.0;
    double a = std::pow(x, -0.75);
    double b = std::pow(1.0 - 1.0 / std::sqrt(x), 0.25);
    return a * b / kNTProfilePeak;
}

// Normalised radiative flux F(x)/F_peak = Theta(x)^4.
inline double ntFlux(double x) {
    double t = ntTheta(x);
    return t * t * t * t;
}

// Peak effective temperature of the disk in Kelvin, given the black hole mass
// (solar masses) and the accretion rate in units of the Eddington rate.
inline double diskPeakTemperature(double massSolar, double eddingtonRatio) {
    double M = massSolar * kMsun;
    double Mdot = eddingtonRatio * eddingtonAccretionRate(massSolar);
    double rin = kIsco * gravitationalRadiusCm(massSolar);  // 6 GM/c^2
    double F0 = 3.0 * kG * M * Mdot / (8.0 * M_PI * rin * rin * rin);
    double T0 = std::pow(F0 / kSigmaSB, 0.25);
    return kNTProfilePeak * T0;
}

// ---------------------------------------------------------------------------
// Circular geodesic kinematics in the equatorial plane.
// ---------------------------------------------------------------------------
// Coordinate angular velocity Omega = sqrt(M / r^3), with M = 1.
inline double keplerOmega(double r) { return std::sqrt(1.0 / (r * r * r)); }

// Time component of the 4-velocity of a circular geodesic: u^t = 1/sqrt(1-3M/r).
inline double circularUt(double r) {
    double v = 1.0 - 3.0 / r;
    return v > 1e-6 ? 1.0 / std::sqrt(v) : 1e6;
}

// Total redshift factor g = nu_obs / nu_emit for a photon with conserved
// b_z = L_z / E striking the disk at radius r:
//   g = 1 / ( u^t (1 - Omega * b_z) )
// This folds together gravitational redshift and Doppler beaming.
inline double redshiftFactor(double r, double bz) {
    double denom = circularUt(r) * (1.0 - keplerOmega(r) * bz);
    if (std::fabs(denom) < 1e-9) return 0.0;
    return 1.0 / denom;
}

// Orbital speed of the circular geodesic measured by a static observer at r,
// in units of c:  beta = sqrt(M/r) / sqrt(1 - 2M/r).  Equals 0.5 at the ISCO.
inline double orbitalBeta(double r) {
    double num = std::sqrt(1.0 / r);
    double den = std::sqrt(std::max(1e-12, 1.0 - kRs / r));
    return num / den;
}

// Angular radius of the shadow seen by a static observer at radius r.
inline double shadowAngularRadius(double r) {
    double s = kBCrit * std::sqrt(std::max(0.0, 1.0 - kRs / r)) / r;
    return std::asin(std::min(1.0, s));
}

}  // namespace bh
