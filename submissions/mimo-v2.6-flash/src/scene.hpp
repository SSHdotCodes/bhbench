// Static / dynamic GPU geometry: the Flamm-paraboloid spacetime sheet, the
// highlight rings, and the equatorial null-geodesic curves for mode 3.
#pragma once

#include <utility>
#include <vector>

// OpenGL entry points for the mesh helpers.
#ifdef _WIN32
#include <GL/glew.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include "math3d.hpp"

namespace bh {

struct LineVtx { float p[3]; float c[4]; float size; };
struct SurfVtx { float p[3]; float n[3]; float curv; };

struct Mesh {
    unsigned vao = 0;
    unsigned vbo = 0;
    int count = 0;                  // total vertices
    unsigned mode = 0;              // GL draw mode
    std::vector<std::pair<int, int>> sub;  // (first, count) ranges; empty => single draw

    void draw() const;
    void update(const void* data, size_t bytes) const;
    void destroy();
    bool valid() const { return vao != 0; }
};

Mesh makeLineMesh(const std::vector<LineVtx>& v, unsigned mode);
Mesh makeSurfMesh(const std::vector<SurfVtx>& v, unsigned mode);

// --- mode 2: spacetime curvature sheet -------------------------------------
Mesh buildFlammSurface(bool mirror, int nR, int nPhi);
Mesh buildFlammGridLines(int nPhiLines, int nR, int nRLines);
Mesh buildCircle(double radius, double z, const float col[4], int nSeg);
Mesh buildDynamicStrip(int nPts);   // particle trail, rewritten each frame
Mesh buildSinglePoint(float size, const float col[4]);

// --- mode 3: null-geodesic gallery -----------------------------------------
struct GeoScene {
    Mesh strips;     // one line strip per geodesic
    Mesh flat;       // GL_LINES: flat-space references + capture boundaries
    Mesh shadow;     // filled horizon disk (triangle fan)
    Mesh circles;    // horizon / photon sphere / ISCO / guide circles
};
GeoScene buildGeoScene();

// Map |K| to a 0..1 value for the colour ramp (log scale, K = -rs/2r^3).
float curvatureParam(double r);

}  // namespace bh
