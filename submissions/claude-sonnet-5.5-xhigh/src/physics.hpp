// physics.hpp -- CPU-side physics: Kerr orbital quantities, Novikov-Thorne disk, blackbody colour tables,
// the embedding diagram of the equatorial plane, and timelike geodesics for the spacetime-funnel view.
// Everything here is double precision; the GPU only receives baked look-up tables.
#pragma once
#include <vector>

namespace phys {

constexpr double PI = 3.14159265358979323846;

double r_horizon(double a);
double r_isco(double a);              // prograde innermost stable circular orbit
double r_photon_pro(double a);        // prograde circular photon orbit (equatorial)
double r_photon_retro(double a);
double r_ergo_equator();              // = 2 M

struct Circ { double omega, E, L, ut; };
Circ circular(double r, double a);    // prograde circular geodesic at radius r

// Page & Thorne (1974) radiative flux from the definition (numerical integral) and from the closed
// form.  Normalised so that Mdot/(4 pi) = 1; large-r limit is 1.5/r^3 (1 - sqrt(r_isco/r)).
double nt_flux_numeric(double r, double a);
double nt_flux_closed(double r, double a);

// (F(r)/F_max)^(1/4) sampled uniformly on [r_isco, r_out]; F_max returned through fmax.
std::vector<float> nt_temperature_table(double a, double r_out, int n, double* fmax_out = nullptr);

// Blackbody radiance -> linear sRGB (via the Wyman et al. CIE 1931 fits).  rgb is normalised to unit
// luminance, log2y is log2(luminance relative to a 6504 K blackbody).
struct BBRow { float r, g, b, log2y; };
// Visible-band luminance of a blackbody at T relative to a 6504 K one (the same scale as BBRow::log2y).
double blackbody_luminance(double T);
std::vector<BBRow> blackbody_table(int n, double Tmin, double Tmax);

// Embedding diagram of the equatorial (t = const, theta = pi/2) slice of Kerr as a surface of revolution.
//   rho(r)^2 = r^2 + a^2 + 2 a^2 / r          (circumferential radius)
//   dz/dr    = sqrt( r^2/Delta - rho'(r)^2 )  (proper radial distance)
// Samples are r = r_plus + w^2, w uniform, so the vertical throat at the horizon is resolved.
struct Embedding {
    std::vector<double> r, rho, z;   // z(r_max) = 0, z < 0 deeper in the well
    bool valid = true;               // false if the slice cannot be embedded (dz/dr imaginary somewhere)
    double throat_rho = 0;           // rho(r_plus) = 2 M
};
Embedding kerr_embedding(double a, double r_max, int n);

// Bardeen (1973) critical curve of a Kerr hole: parametric (alpha, beta) for photon-orbit radius r_p.
void bardeen_curve(double a, double theta0, double r_p, double* alpha, double* beta);
// Constants of motion (L/E, Q/E^2) of the circular photon orbit of radius r_p (the critical curve in (L,Q) space).
void bardeen_LQ(double a, double r_p, double* L, double* Q);

// Equatorial null geodesic (forward-in-time photon falling in from r_start with angular momentum L per unit energy).
struct PhotonPath {
    std::vector<double> r, phi;
    bool captured = false;
};
PhotonPath photon_path_equatorial(double a, double L, double r_start, double phi_start);

// ---- Timelike equatorial geodesics (test particles) ------------------------------------------------
struct Track {
    double dt = 0.05;                // uniform sampling in coordinate time
    std::vector<double> r, phi;
    bool plunged = false;            // ends by falling toward the horizon
    double duration() const { return dt * (r.size() ? r.size() - 1 : 0); }
    void sample(double t, bool loop, double* r_out, double* phi_out) const;
};
// Circular orbit at radius r.
Track circular_track(double a, double r, double phi0, double t_max);
// Bound orbit with turning points r_p < r_a (starts at apoapsis).  Returns false if not found.
bool bound_orbit_constants(double a, double r_p, double r_a, double* E, double* L);
// Integrate starting at radius r_start moving inward (or outward), given E, L.  Stops at t_max or when the
// particle reaches r_plus + eps.
Track integrate_track(double a, double E, double L, double r_start, bool inward, double phi0, double t_max);
double energy_from_apoapsis(double a, double r_a, double L);   // E such that R_t(r_a) = 0 (prograde root)

}  // namespace phys
