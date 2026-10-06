#include "scene.hpp"

#include <cmath>
#include <cstddef>

#include "physics.hpp"

namespace bh {

namespace {
constexpr double kGLLineStrip = 0x0003;
constexpr double kGLTriangles = 0x0004;
constexpr double kGLLines = 0x0001;
constexpr double kGLTriangleFan = 0x0006;
constexpr double kGLLineLoop = 0x0002;
constexpr double kGLPoints = 0x0000;

// inner radius of the embedding sheet (just outside the throat) and grid range
constexpr double kRMinFactor = 1.0 + 1e-4;   // r = rs * (1 + eps)
constexpr double kRMax = 60.0;               // outer edge of the drawn sheet
constexpr int kNR = 150;                     // radial samples (uniform in s = sqrt(r-rs))

inline double rFromS(double s) { return RS + s * s; }
inline double sMin() { return std::sqrt(RS * (kRMinFactor - 1.0)); }
inline double sMax() { return std::sqrt(kRMax - RS); }
}  // namespace

float curvatureParam(double r) {
    const double lo = 1e-6, hi = 2e-1;
    double a = std::fabs(gaussianCurvature(r));
    double t = (std::log10(a) - std::log10(lo)) / (std::log10(hi) - std::log10(lo));
    return float(t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
}

// ---------------------------------------------------------------------------
// Mesh plumbing
// ---------------------------------------------------------------------------
void Mesh::draw() const {
    glBindVertexArray(vao);
    if (sub.empty()) {
        glDrawArrays(GLenum(mode), 0, count);
    } else {
        for (const auto& r : sub) glDrawArrays(GLenum(mode), r.first, r.second);
    }
}

void Mesh::update(const void* data, size_t bytes) const {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data);
}

void Mesh::destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = 0;
    count = 0;
    sub.clear();
}

Mesh makeLineMesh(const std::vector<LineVtx>& v, unsigned mode) {
    Mesh m;
    m.mode = mode;
    m.count = int(v.size());
    if (v.empty()) return m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(LineVtx)), v.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVtx),
                          reinterpret_cast<void*>(offsetof(LineVtx, p)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVtx),
                          reinterpret_cast<void*>(offsetof(LineVtx, c)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(LineVtx),
                          reinterpret_cast<void*>(offsetof(LineVtx, size)));
    glBindVertexArray(0);
    return m;
}

Mesh makeSurfMesh(const std::vector<SurfVtx>& v, unsigned mode) {
    Mesh m;
    m.mode = mode;
    m.count = int(v.size());
    if (v.empty()) return m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(SurfVtx)), v.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfVtx),
                          reinterpret_cast<void*>(offsetof(SurfVtx, p)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfVtx),
                          reinterpret_cast<void*>(offsetof(SurfVtx, n)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SurfVtx),
                          reinterpret_cast<void*>(offsetof(SurfVtx, curv)));
    glBindVertexArray(0);
    return m;
}

// ---------------------------------------------------------------------------
// Mode 2 geometry
// ---------------------------------------------------------------------------
Mesh buildFlammSurface(bool mirror, int nR, int nPhi) {
    std::vector<SurfVtx> v;
    v.reserve(size_t(nR) * size_t(nPhi) * 6);

    auto vert = [&](double s, double phi, SurfVtx& o) {
        double r = rFromS(s);
        double z = flammZ(r) * (mirror ? -1.0 : 1.0);
        double sl = flammSlope(r) * (mirror ? -1.0 : 1.0);
        float cp = float(std::cos(phi)), sp = float(std::sin(phi));
        // world convention: the sheet's axis is +Z (same as the disk normal)
        o.p[0] = float(r) * cp;
        o.p[1] = float(r) * sp;
        o.p[2] = float(z);
        // Outward (up-facing) normal of the surface of revolution z(r):
        //   N = normalize(-z' cos phi, -z' sin phi, 1)
        float nx = float(-sl) * cp, ny = float(-sl) * sp, nz = 1.0f;
        float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
        o.n[0] = nx * inv;
        o.n[1] = ny * inv;
        o.n[2] = nz * inv;
        o.curv = curvatureParam(r);
    };

    double ds = (sMax() - sMin()) / double(nR - 1);
    double dphi = 2.0 * M_PI / double(nPhi);
    for (int i = 0; i < nR - 1; ++i) {
        double s0 = sMin() + ds * i, s1 = s0 + ds;
        for (int j = 0; j < nPhi; ++j) {
            double p0 = dphi * j, p1 = p0 + dphi;
            SurfVtx a, b, c, d;
            vert(s0, p0, a);
            vert(s1, p0, b);
            vert(s1, p1, c);
            vert(s0, p1, d);
            v.push_back(a); v.push_back(b); v.push_back(c);
            v.push_back(a); v.push_back(c); v.push_back(d);
        }
    }
    return makeSurfMesh(v, unsigned(kGLTriangles));
}

Mesh buildFlammGridLines(int nPhiLines, int nR, int nRLines) {
    std::vector<LineVtx> v;
    std::vector<std::pair<int, int>> ranges;

    const float dim[4] = {0.42f, 0.72f, 1.0f, 0.85f};

    auto push = [&](double r, double phi) {
        double z = flammZ(r);
        LineVtx o{};
        o.p[0] = float(r * std::cos(phi));
        o.p[1] = float(r * std::sin(phi));
        o.p[2] = float(z);
        float t = curvatureParam(r);
        o.c[0] = dim[0] * (0.85f + 0.75f * t);
        o.c[1] = dim[1] * (0.85f + 0.75f * t);
        o.c[2] = dim[2] * (0.95f + 0.55f * t);
        o.c[3] = dim[3];
        o.size = 0.f;
        v.push_back(o);
    };

    double ds = (sMax() - sMin()) / double(nR - 1);

    // circles of constant Schwarzschild r (equally spaced in sqrt(r-rs))
    for (int i = 0; i < nRLines; ++i) {
        double s = sMin() + ds * double(i) * double(nR - 1) / double(nRLines - 1);
        double r = rFromS(s);
        int first = int(v.size());
        const int seg = 128;
        for (int j = 0; j <= seg; ++j) push(r, 2.0 * M_PI * j / seg);
        ranges.emplace_back(first, int(v.size()) - first);
    }
    // radials of constant phi
    for (int k = 0; k < nPhiLines; ++k) {
        double phi = 2.0 * M_PI * k / nPhiLines;
        int first = int(v.size());
        for (int i = 0; i < nR; ++i) push(rFromS(sMin() + ds * i), phi);
        ranges.emplace_back(first, int(v.size()) - first);
    }
    Mesh out = makeLineMesh(v, unsigned(kGLLineStrip));
    out.sub = ranges;
    return out;
}

Mesh buildCircle(double radius, double z, const float col[4], int nSeg) {
    std::vector<LineVtx> v;
    for (int j = 0; j <= nSeg; ++j) {
        double a = 2.0 * M_PI * j / nSeg;
        LineVtx o{};
        o.p[0] = float(radius * std::cos(a));
        o.p[1] = float(radius * std::sin(a));
        o.p[2] = float(z);
        o.c[0] = col[0]; o.c[1] = col[1]; o.c[2] = col[2]; o.c[3] = col[3];
        o.size = 0.f;
        v.push_back(o);
    }
    return makeLineMesh(v, unsigned(kGLLineStrip));
}

Mesh buildDynamicStrip(int nPts) {
    std::vector<LineVtx> v;
    v.resize(size_t(nPts));
    Mesh m = makeLineMesh(v, unsigned(kGLLineStrip));
    m.count = nPts;
    return m;
}

Mesh buildSinglePoint(float size, const float col[4]) {
    LineVtx v{};
    v.c[0] = col[0]; v.c[1] = col[1]; v.c[2] = col[2]; v.c[3] = col[3];
    v.size = size;
    return makeLineMesh({v}, unsigned(kGLPoints));
}

// ---------------------------------------------------------------------------
// Mode 3 geometry: null geodesics in the equatorial plane
// ---------------------------------------------------------------------------
GeoScene buildGeoScene() {
    GeoScene g;
    const double rStart = 45.0;
    const float dim[4] = {0.45f, 0.50f, 0.60f, 0.30f};

    const double bs[] = {1.0, 2.0, 3.0, 4.0, 4.5, 4.8, 5.0, 5.12, 5.17, 5.193,
                         5.1961, 5.21, 5.3, 5.6, 6.0, 6.8, 8.0, 10.0, 13.0, 17.0,
                         22.0, 29.0, 38.0};
    const int nB = int(sizeof(bs) / sizeof(bs[0]));

    // geodesic strips
    std::vector<LineVtx> strips;
    std::vector<std::pair<int, int>> ranges;
    for (int i = 0; i < nB; ++i) {
        double b = bs[i];
        double t = b / B_CRIT;
        Polyline pl = traceEquatorialRay(b, rStart, 70.0, 400000);
        if (pl.pts.size() < 2) continue;

        float col[4];
        if (pl.captured) {
            double q = t < 1.0 ? t : 1.0;
            col[0] = float(0.85 + 0.15 * q);
            col[1] = float(0.16 + 0.45 * q);
            col[2] = float(0.08 + 0.10 * q);
        } else {
            double q = (t - 1.0) / 3.0;
            q = q < 0.0 ? 0.0 : (q > 1.0 ? 1.0 : q);
            col[0] = float(1.00 - 0.75 * q);
            col[1] = float(0.92 - 0.45 * q);
            col[2] = float(0.25 + 0.70 * q);
        }
        col[3] = pl.captured ? 0.95f : 0.90f;

        int first = int(strips.size());
        for (const auto& p : pl.pts) {
            LineVtx o{};
            o.p[0] = float(p[0]);
            o.p[1] = float(p[1]);
            o.p[2] = 0.f;
            o.c[0] = col[0]; o.c[1] = col[1]; o.c[2] = col[2]; o.c[3] = col[3];
            o.size = 0.f;
            strips.push_back(o);
        }
        ranges.emplace_back(first, int(strips.size()) - first);
    }
    g.strips = makeLineMesh(strips, unsigned(kGLLineStrip));
    g.strips.sub = ranges;

    // flat-space reference lines + impact-parameter boundaries
    std::vector<LineVtx> flat;
    auto seg = [&](float x0, float y0, float x1, float y1, const float* c, float a) {
        LineVtx v0{}, v1{};
        v0.p[0] = x0; v0.p[1] = y0; v0.p[2] = 0.f;
        v1.p[0] = x1; v1.p[1] = y1; v1.p[2] = 0.f;
        for (int k = 0; k < 3; ++k) { v0.c[k] = c[k]; v1.c[k] = c[k]; }
        v0.c[3] = v1.c[3] = a;
        flat.push_back(v0);
        flat.push_back(v1);
    };
    for (int i = 0; i < nB; ++i) seg(float(-rStart), float(bs[i]), float(rStart), float(bs[i]), dim, 0.35f);
    // capture cross section |y| < b_c
    const float crit[3] = {1.0f, 0.25f, 0.15f};
    seg(float(-rStart), float(B_CRIT), float(rStart), float(B_CRIT), crit, 0.55f);
    seg(float(-rStart), float(-B_CRIT), float(rStart), float(-B_CRIT), crit, 0.55f);
    g.flat = makeLineMesh(flat, unsigned(kGLLines));

    // filled horizon disk
    {
        std::vector<LineVtx> disk;
        const int segs = 96;
        LineVtx c{};
        c.p[0] = c.p[1] = 0.f; c.p[2] = 0.f;
        c.c[0] = 0.02f; c.c[1] = 0.02f; c.c[2] = 0.03f; c.c[3] = 1.f;
        disk.push_back(c);
        for (int j = 0; j <= segs; ++j) {
            double a = 2.0 * M_PI * j / segs;
            LineVtx o{};
            o.p[0] = float(RS * std::cos(a));
            o.p[1] = float(RS * std::sin(a));
            o.p[2] = 0.f;
            o.c[0] = 0.04f; o.c[1] = 0.02f; o.c[2] = 0.05f; o.c[3] = 1.f;
            disk.push_back(o);
        }
        g.shadow = makeLineMesh(disk, unsigned(kGLTriangleFan));
    }

    // guide circles: horizon, photon sphere, ISCO + faint orbit guides
    {
        std::vector<LineVtx> v;
        std::vector<std::pair<int, int>> ranges;
        auto circle = [&](double r, const float* c, float a) {
            int first = int(v.size());
            const int seg = 160;
            for (int j = 0; j <= seg; ++j) {
                double ang = 2.0 * M_PI * j / seg;
                LineVtx o{};
                o.p[0] = float(r * std::cos(ang));
                o.p[1] = float(r * std::sin(ang));
                o.p[2] = 0.f;
                o.c[0] = c[0]; o.c[1] = c[1]; o.c[2] = c[2]; o.c[3] = a;
                o.size = 0.f;
                v.push_back(o);
            }
            ranges.emplace_back(first, int(v.size()) - first);
        };
        const float hor[3] = {1.0f, 0.30f, 0.25f};
        const float pho[3] = {1.0f, 0.70f, 0.20f};
        const float isco[3] = {0.35f, 1.0f, 0.55f};
        const float guide[3] = {0.35f, 0.40f, 0.50f};
        circle(RS, hor, 0.90f);
        circle(R_PHOTON, pho, 0.75f);
        circle(R_ISCO, isco, 0.65f);
        for (double r : {10.0, 20.0, 30.0, 40.0}) circle(r, guide, 0.30f);
        g.circles = makeLineMesh(v, unsigned(kGLLineLoop));
        g.circles.sub = ranges;
    }

    return g;
}

}  // namespace bh
