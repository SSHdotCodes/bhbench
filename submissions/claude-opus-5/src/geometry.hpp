// geometry.hpp — CPU-built geometry for the two spacetime-curvature views.
//
//  * The embedding ("Flamm") funnel: an isometric picture of the curved
//    geometry of the equatorial plane, drawn as a grid so the stretching of
//    space is directly visible.  The throat is the horizon: a trapdoor.
//
//  * The light-cone field: the future light cone of the Kerr metric sampled
//    across the equatorial plane, with coordinate time on the vertical axis.
//    Cones tip inward as r falls, become tangent to the surface of constant r
//    at the horizon, and inside it every future direction has dr < 0.

#pragma once
#include <vector>
#include <OpenGL/gl3.h>
#include "mathx.hpp"

namespace geo {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float cr, cg, cb, ca;
};

// A GPU mesh with an interleaved Vertex layout.
struct Mesh {
    GLuint vao = 0, vbo = 0;
    GLsizei count = 0;
    GLenum mode = GL_TRIANGLES;

    void upload(const std::vector<Vertex>& v, GLenum drawMode);
    void draw() const;
    void destroy();
};

// ---- embedding diagram ----

// Circumferential radius of the equatorial circle of Boyer-Lindquist radius r.
double embedR(double r, double a);

// Height of the embedding surface, integrated inward from rMax.  For a >~ 0.72
// the Kerr equatorial plane stops being embeddable in Euclidean 3-space near
// the horizon; `clamped` reports whether that happened.
std::vector<double> embedZ(const std::vector<double>& rs, double a, bool* clamped);

// Kretschmann curvature invariant, the coordinate-independent measure of tidal
// curvature.  Equatorial Schwarzschild limit: K = 48 M^2 / r^6.
double kretschmann(double r, double theta, double a);

void buildFunnel(Mesh& surface, Mesh& gridLines, Mesh& rings,
                 double a, double rMax, int nr, int nphi, bool* clamped);

// Depth of the funnel throat below the outer rim, for framing the camera.
double funnelDepth();

// ---- light cones ----

void buildLightCones(Mesh& cones, Mesh& floorGrid, Mesh& markers,
                     double a, double rMax);

// ---- geodesics ----

struct Trajectory {
    std::vector<Vec3> pts;      // already mapped into the funnel's 3-D space
    float r = 1, g = 1, b = 1;
    bool captured = false;
};

// Timelike test particles released around the hole, integrated as exact Kerr
// geodesics and then drawn on the embedding surface.
std::vector<Trajectory> buildTestParticles(double a, double rMax, int count);

// Null geodesics (photons) fired past the hole at a range of impact parameters.
std::vector<Trajectory> buildPhotonPaths(double a, double rMax, int count);

void uploadTrajectories(Mesh& mesh, const std::vector<Trajectory>& trs, float alpha);

}  // namespace geo
