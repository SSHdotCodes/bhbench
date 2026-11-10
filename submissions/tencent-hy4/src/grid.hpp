// ============================================================================
//  grid.hpp  --  Build the wireframe for the "trapdoor in spacetime" view.
//
//  We embed the equatorial (theta = pi/2) spatial slice of the Schwarzschild
//  metric into Euclidean 3-space as a surface of revolution. Matching the
//  induced metric
//
//      dl^2 = dr^2/(1 - rs/r) + r^2 dphi^2   (Schwarzschild equatorial slice)
//      dl^2 = (1 + (dw/dr)^2) dr^2 + r^2 dphi^2   (surface w(r) in 3-space)
//
//  gives  dw/dr = +/- sqrt(rs/(r-rs))  =>  w(r) = +/- 2 sqrt(rs (r - rs)).
//
//  This is FLAMM'S PARABOLOID: two sheets (+/- w) joined at the throat r = rs,
//  w = 0 -- the Einstein-Rosen bridge, the literal "trapdoor" in spacetime.
//  Near the throat dw/dr diverges (the wall becomes vertical), which is the
//  visual signature of the infinite stretching of space at the horizon.
//
//  Vertex format (5 floats):  x, y(=w), z, t, sheet
//     t     : normalized radius (0 at throat r=rs, 1 at outer edge rMax)
//     sheet : 0 = upper sheet, 1 = lower (mirror) sheet
//  Drawn as GL_LINES (pairs of endpoints).
// ============================================================================
#ifndef BH_GRID_HPP
#define BH_GRID_HPP

#include <vector>
#include <cmath>
#include "physics.hpp"

namespace bh {

inline void pushVertex(std::vector<float>& v, double r, double phi,
                       double sheet, double rMax){
    double w = 2.0 * std::sqrt(RS * (r - RS));        // Flamm embedding depth
    if (sheet > 0.5) w = -w;                           // lower (mirror) sheet
    double x = r * std::cos(phi);
    double z = r * std::sin(phi);
    double t = (r - RS) / (rMax - RS);
    v.push_back((float)x);
    v.push_back((float)w);
    v.push_back((float)z);
    v.push_back((float)t);
    v.push_back((float)sheet);
}

// Append one line segment (two endpoints) to the vertex list.
inline void pushSegment(std::vector<float>& v,
                        double r0, double p0, double r1, double p1,
                        double sheet, double rMax){
    pushVertex(v, r0, p0, sheet, rMax);
    pushVertex(v, r1, p1, sheet, rMax);
}

// Generate the full two-sheet wireframe. Returns number of vertices.
inline size_t buildGrid(std::vector<float>& out, double rMax = 9.0){
    out.clear();
    const int NRINGS = 30;      // number of concentric latitude rings
    const int RSEG   = 96;      // segments per ring
    const int NSPOKE = 40;      // number of meridian spokes
    const int SSEG   = 64;      // segments per meridian
    const double PI  = 3.14159265358979323846;

    // radius samples: linear in coordinate r (shows the steepening / stretching)
    auto radius = [&](int i, int n)->double{
        double f = double(i) / double(n);
        return RS + (rMax - RS) * f + 1e-3;           // +eps avoids r == rs
    };

    for (int sheet = 0; sheet < 2; ++sheet){
        // --- concentric rings (constant r) ---
        for (int ri = 1; ri <= NRINGS; ++ri){
            double r = radius(ri, NRINGS);
            for (int s = 0; s < RSEG; ++s){
                double p0 = 2.0 * PI * double(s)     / RSEG;
                double p1 = 2.0 * PI * double(s + 1) / RSEG;
                pushSegment(out, r, p0, r, p1, sheet, rMax);
            }
        }
        // --- meridian spokes (constant phi): polylines following w(r) ---
        for (int sp = 0; sp < NSPOKE; ++sp){
            double phi = 2.0 * PI * double(sp) / NSPOKE;
            for (int s = 0; s < SSEG; ++s){
                double r0 = radius(s,     SSEG);
                double r1 = radius(s + 1, SSEG);
                pushSegment(out, r0, phi, r1, phi, sheet, rMax);
            }
        }
    }
    return out.size() / 5;   // number of vertices
}

} // namespace bh
#endif // BH_GRID_HPP
