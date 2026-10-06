// physics.h — host-side (double precision) astrophysics for the renderer and the validation suite.
#pragma once
#include <string>
#include <vector>

namespace phys {

// SI constants (CODATA 2018 / IAU)
constexpr double G = 6.67430e-11;
constexpr double C = 2.99792458e8;
constexpr double SIGMA_SB = 5.670374419e-8;
constexpr double M_SUN = 1.98847e30;
constexpr double M_PROTON = 1.67262192e-27;
constexpr double SIGMA_THOMSON = 6.6524587e-29;
constexpr double PI = 3.14159265358979323846;

// ---- Kerr geometry (G = c = M = 1, signed spin a; a > 0: disk co-rotates with the hole) ----
double horizon(double a);                 // r_+
double isco(double a);                    // Bardeen–Press–Teukolsky r_ms
double photonOrbit(double a, int prograde);  // circular equatorial photon orbit (|a|), prograde = +1 / −1
double circE(double a, double r);         // specific energy of the circular equatorial orbit (orbiting +φ)
double circL(double a, double r);         // specific angular momentum
double circOmega(double a, double r);     // dφ/dt
double equatorialCircumferentialRadius(double a, double r);   // sqrt(g_φφ) at θ = π/2

// ---- Novikov–Thorne thin disk (Page & Thorne 1974) ----
// Dimensionless flux F̃(r) from ONE face, per unit rest-mass accretion rate: F = (Ṁ c⁶ / G²M²) F̃.
// Computed from first principles: F̃ = −(Ω,r / (4π r (E − ΩL)²)) ∫_{r_ms}^{r} (E − ΩL) L,r dr.
struct DiskModel {
    double a = 0, rin = 6, rout = 20;
    std::vector<float> flux;   // F̃(r)/F̃_max sampled at r_i = rin + (rout − rin)(i/(N−1))²
    double fluxMax = 0;        // F̃_max (dimensionless)
    double rPeak = 0;          // radius of maximum flux
};
DiskModel buildDisk(double a, double rout, int samples);
double ntFluxDirect(double a, double r);      // slow direct evaluation (reference for tests)
double ntFluxSchwarzschild(double r);         // closed form, a = 0 (derived in physics.cpp)
double radiativeEfficiency(double a);         // η = 1 − E_ms
double diskLuminosityIntegral(double a, double rmax);   // ∫ 4π r E F̃ dr  → η as rmax → ∞ (energy check)

// Physical temperature scale.  T(r) = [Ṁ c⁶ F̃(r) / (G² M² σ)]^{1/4};  ṁ = Ṁ / Ṁ_Edd with Ṁ_Edd = L_Edd / (η c²).
double eddingtonLuminosity(double massSolar);                                   // W
double peakTemperature(double a, double massSolar, double mdotEdd, double fluxMax);   // K
double mdotForPeakTemperature(double a, double massSolar, double Tpeak, double fluxMax);
double gravitationalTimeSeconds(double massSolar);                               // GM/c³

// ---- Embedding diagram of the equatorial t = const slice ----
// ds² = (r²/Δ) dr² + R(r)² dφ²  embedded as a surface of revolution (R, z(R)) in flat R³:
//   dz/dr = sqrt(r²/Δ − (dR/dr)²).  a = 0 gives Flamm's paraboloid z = sqrt(8(r − 2)).
struct Funnel {
    double R0 = 2, R1 = 30;     // throat (horizon) and outer circumferential radii
    std::vector<float> Z;       // surface height sampled at R_i = R0 + (R1 − R0)(i/(N−1))²
    double depth = 0;           // z(R1) − z(R0)
    int nonEmbeddable = 0;      // samples where the slice cannot be embedded (clamped)
};
Funnel buildFunnel(double a, double R1, double top, double scale, int samples);
double embeddingHeight(double a, double r);   // z(r) − z(r_+) (reference for tests)

std::string formatSI(double v, int digits = 3);

}  // namespace phys
