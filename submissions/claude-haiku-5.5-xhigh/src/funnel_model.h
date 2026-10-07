// Host-side geometry of the rubber-sheet ("spacetime trapdoor") view.
//
// The equatorial slice (theta = pi/2, t = const) of Kerr has the induced 2-metric
//   dl^2 = (r^2 / Delta) dr^2 + rho(r)^2 dphi^2,     rho(r)^2 = r^2 + a^2 + 2 a^2 / r,
// with Delta = r^2 - 2r + a^2. Embedding it in cylindrical coordinates (rho, phi, Y) gives
//   dY/dr = sqrt( r^2 / Delta - (d rho / dr)^2 ),
// which for a = 0 reproduces Flamm's paraboloid Y = 2 sqrt(2 (r - 2)). The throat at the horizon
// Y(r_+) is the bottom of the funnel; Y is measured so the rim at r_view sits at Y = 0.
#ifndef BH_FUNNEL_MODEL_H
#define BH_FUNNEL_MODEL_H

#include <algorithm>
#include <cmath>
#include <vector>
#include "shared_types.h"

struct FunnelProfile {
    double r_plus;
    double r_minus;
    double r_view;
    // Samples indexed by the parameter s in [0,1], where r = r_+ + (r_view - r_+) s^2.
    std::vector<double> s;
    std::vector<double> r;
    std::vector<double> rho;
    std::vector<double> Y;      // height relative to the rim
};

inline double rho_of_r(double a, double r) { return std::sqrt(r * r + a * a + 2.0 * a * a / r); }

inline FunnelProfile build_funnel_profile(double a, double r_view, int ns) {
    FunnelProfile p;
    double root = std::sqrt(1.0 - a * a);
    p.r_plus = 1.0 + root;
    p.r_minus = 1.0 - root;
    p.r_view = r_view;
    double span = r_view - p.r_plus;

    // dY/ds = sqrt(r^2 (dr/ds)^2 / Delta - rho'^2 (dr/ds)^2) with r = r_+ + span s^2, dr/ds = 2 s span.
    // Using Delta = (r - r_+)(r - r_-) and (r - r_+) = span s^2 gives (dr/ds)^2 / Delta = 4 span / (r - r_-),
    // which stays finite at the horizon, so the integrand has no singularity at s = 0.
    auto dYds = [&](double s) {
        double r = p.r_plus + span * s * s;
        double rho = rho_of_r(a, r);
        double drho = (r - a * a / (r * r)) / rho;
        double inside = 4.0 * r * r * span / (r - p.r_minus) - 4.0 * drho * drho * s * s * span * span;
        return std::sqrt(std::max(inside, 0.0));
    };

    p.s.resize(ns + 1);
    p.r.resize(ns + 1);
    p.rho.resize(ns + 1);
    p.Y.assign(ns + 1, 0.0);
    std::vector<double> Ys(ns + 1, 0.0);
    double gPrev = dYds(0.0);
    for (int k = 0; k <= ns; ++k) {
        double s = double(k) / double(ns);
        double r = p.r_plus + span * s * s;
        p.s[k] = s;
        p.r[k] = r;
        p.rho[k] = rho_of_r(a, r);
        if (k > 0) {
            double g = dYds(s);
            Ys[k] = Ys[k - 1] + 0.5 * (g + gPrev) * (s - p.s[k - 1]);
            gPrev = g;
        }
    }
    double top = Ys[ns];
    for (int k = 0; k <= ns; ++k) {
        p.Y[k] = Ys[k] - top;
    }
    return p;
}

// Builds the triangle list for the sheet: rings along s, sectors along phi. Each vertex carries
// (r, phi) as its parameter so the fragment shader can draw grid lines in those coordinates.
inline std::vector<FunnelVertex> build_funnel_mesh(const FunnelProfile& p, int nphi) {
    int ns = int(p.s.size()) - 1;
    std::vector<FunnelVertex> v;
    v.reserve(size_t(ns) * size_t(nphi) * 6);
    const double twoPi = 2.0 * M_PI;
    auto vertex = [&](int i, int j) {
        double phi = twoPi * double(j) / double(nphi);
        FunnelVertex out;
        out.pos[0] = float(p.rho[i] * std::cos(phi));
        out.pos[1] = float(p.Y[i]);
        out.pos[2] = float(p.rho[i] * std::sin(phi));
        out.pos[3] = 1.0f;
        out.par[0] = float(p.r[i]);
        out.par[1] = float(phi);
        out.par[2] = 0.0f;
        out.par[3] = 0.0f;
        return out;
    };
    for (int i = 0; i < ns; ++i) {
        for (int j = 0; j < nphi; ++j) {
            FunnelVertex a = vertex(i, j);
            FunnelVertex b = vertex(i + 1, j);
            FunnelVertex c = vertex(i + 1, j + 1);
            FunnelVertex d = vertex(i, j + 1);
            v.push_back(a);
            v.push_back(b);
            v.push_back(c);
            v.push_back(a);
            v.push_back(c);
            v.push_back(d);
        }
    }
    return v;
}

#endif
