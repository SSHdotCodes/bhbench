#include "geometry.hpp"
#include "kerr.hpp"
#include <cmath>
#include <algorithm>

namespace geo {

// ---------------------------------------------------------------- Mesh ----

void Mesh::upload(const std::vector<Vertex>& v, GLenum drawMode) {
    mode = drawMode;
    count = (GLsizei)v.size();
    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex),
                 v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (!count) return;
    glBindVertexArray(vao);
    glDrawArrays(mode, 0, count);
    glBindVertexArray(0);
}

void Mesh::destroy() {
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    count = 0;
}

// --------------------------------------------------------- embedding ------

double embedR(double r, double a) {
    return std::sqrt(r * r + a * a + 2.0 * a * a / std::max(r, 1e-6));
}

static double dEmbedR(double r, double a) {
    double R = embedR(r, a);
    return (r - a * a / (r * r)) / std::max(R, 1e-9);
}

std::vector<double> embedZ(const std::vector<double>& rs, double a, bool* clamped) {
    // dz/dr = sqrt( g_rr - (dR/dr)^2 ),  g_rr = r^2/Delta on the equator.
    // Integrate inward from the outermost sample with z = 0 there.
    size_t n = rs.size();
    std::vector<double> z(n, 0.0);
    if (clamped) *clamped = false;
    for (size_t i = n - 1; i-- > 0;) {
        double r0 = rs[i + 1], r1 = rs[i];
        double dr = r1 - r0;                   // negative, going inward
        // Simpson over the sub-interval.
        auto slope = [&](double r) {
            double Del = r * r - 2.0 * r + a * a;
            if (Del <= 1e-9) return 0.0;
            double v = r * r / Del - dEmbedR(r, a) * dEmbedR(r, a);
            if (v < 0.0) { if (clamped) *clamped = true; return 0.0; }
            return std::sqrt(v);
        };
        double s0 = slope(r0), sm = slope(0.5 * (r0 + r1)), s1 = slope(r1);
        z[i] = z[i + 1] + dr * (s0 + 4.0 * sm + s1) / 6.0;
    }
    return z;
}

double kretschmann(double r, double theta, double a) {
    double c = std::cos(theta);
    double a2c2 = a * a * c * c;
    double Sig = r * r + a2c2;
    double num = 48.0 * (r * r - a2c2) * (Sig * Sig - 16.0 * r * r * a2c2);
    return num / std::pow(std::max(Sig, 1e-9), 6.0);
}

// Profile shared between the funnel mesh and the trajectories drawn on it.
namespace {
struct Profile {
    std::vector<double> r, R, z;
    double a = 0, rMin = 2, rMax = 30;
    bool built = false;

    void build(double spin, double rmax) {
        a = spin;
        rMax = rmax;
        rMin = kerr::horizonOuter(a) * 1.0001;
        const int N = 900;
        r.resize(N); R.resize(N);
        for (int i = 0; i < N; ++i) {
            // Bunch samples toward the throat, where the surface is steepest.
            double f = double(i) / double(N - 1);
            double t = f * f;
            r[i] = rMin + (rMax - rMin) * t;
            R[i] = embedR(r[i], a);
        }
        bool cl;
        z = embedZ(r, a, &cl);
        built = true;
    }

    double zAt(double rr) const {
        if (!built) return 0.0;
        if (rr <= r.front()) return z.front();
        if (rr >= r.back()) return z.back();
        auto it = std::lower_bound(r.begin(), r.end(), rr);
        size_t i = (size_t)std::max<long>(1, it - r.begin());
        double f = (rr - r[i - 1]) / std::max(1e-12, r[i] - r[i - 1]);
        return z[i - 1] + f * (z[i] - z[i - 1]);
    }
};
Profile gProfile;

Vertex mkv(Vec3 p, Vec3 n, float r, float g, float b, float a) {
    return {p.x, p.y, p.z, n.x, n.y, n.z, r, g, b, a};
}

// Colour ramp for log10 of the Kretschmann scalar: deep blue (flat) to hot
// white (extreme tidal curvature).
Vec3 curvatureColor(double logK) {
    // K runs from ~1e-7 at r = 30 M to order unity at the horizon, so spread the
    // ramp over that whole range instead of clipping the outer half to black.
    float t = (float)std::clamp((logK + 7.6) / 8.6, 0.0, 1.0);
    Vec3 c0{0.02f, 0.05f, 0.16f};
    Vec3 c1{0.07f, 0.26f, 0.62f};
    Vec3 c2{0.52f, 0.20f, 0.55f};
    Vec3 c3{0.92f, 0.42f, 0.20f};
    Vec3 c4{1.00f, 0.74f, 0.30f};
    if (t < 0.25f) return c0 + (c1 - c0) * (t / 0.25f);
    if (t < 0.55f) return c1 + (c2 - c1) * ((t - 0.25f) / 0.30f);
    if (t < 0.80f) return c2 + (c3 - c2) * ((t - 0.55f) / 0.25f);
    return c3 + (c4 - c3) * ((t - 0.80f) / 0.20f);
}
}  // namespace

static void surfacePoint(double r, double phi, double a, Vec3& p, Vec3& n) {
    double R = embedR(r, a);
    double z = gProfile.zAt(r);
    double cp = std::cos(phi), sp = std::sin(phi);
    p = Vec3((float)(R * cp), (float)(R * sp), (float)z);

    double dR = dEmbedR(r, a);
    double Del = r * r - 2.0 * r + a * a;
    double v = (Del > 1e-9) ? (r * r / Del - dR * dR) : 0.0;
    double dz = std::sqrt(std::max(v, 0.0));
    Vec3 nn((float)(-dz * R * cp), (float)(-dz * R * sp), (float)(dR * R));
    n = normalize(nn);
}

void buildFunnel(Mesh& surface, Mesh& gridLines, Mesh& rings,
                 double a, double rMax, int nr, int nphi, bool* clamped) {
    gProfile.build(a, rMax);
    if (clamped) {
        bool cl = false;
        embedZ(gProfile.r, a, &cl);
        *clamped = cl;
    }

    double rMin = gProfile.rMin;

    auto radiusAt = [&](int i) {
        double f = double(i) / double(nr);
        return rMin + (rMax - rMin) * f * f;   // match the profile's bunching
    };

    // ---- shaded surface ----
    std::vector<Vertex> tri;
    tri.reserve(size_t(nr) * nphi * 6);
    for (int i = 0; i < nr; ++i) {
        double r0 = radiusAt(i), r1 = radiusAt(i + 1);
        for (int j = 0; j < nphi; ++j) {
            double p0 = 2.0 * kerr::PI * j / nphi;
            double p1 = 2.0 * kerr::PI * (j + 1) / nphi;
            Vec3 P[4], N[4];
            surfacePoint(r0, p0, a, P[0], N[0]);
            surfacePoint(r1, p0, a, P[1], N[1]);
            surfacePoint(r1, p1, a, P[2], N[2]);
            surfacePoint(r0, p1, a, P[3], N[3]);
            double kr0 = std::log10(std::max(kretschmann(r0, kerr::PI / 2, a), 1e-12));
            double kr1 = std::log10(std::max(kretschmann(r1, kerr::PI / 2, a), 1e-12));
            Vec3 c0 = curvatureColor(kr0), c1 = curvatureColor(kr1);
            tri.push_back(mkv(P[0], N[0], c0.x, c0.y, c0.z, 1));
            tri.push_back(mkv(P[1], N[1], c1.x, c1.y, c1.z, 1));
            tri.push_back(mkv(P[2], N[2], c1.x, c1.y, c1.z, 1));
            tri.push_back(mkv(P[0], N[0], c0.x, c0.y, c0.z, 1));
            tri.push_back(mkv(P[2], N[2], c1.x, c1.y, c1.z, 1));
            tri.push_back(mkv(P[3], N[3], c0.x, c0.y, c0.z, 1));
        }
    }
    surface.upload(tri, GL_TRIANGLES);

    // ---- the grid itself ----
    // The surface is drawn with a polygon depth offset, so the overlays sit
    // exactly on it with no lift -- lifting along the normal would bury them
    // inside the funnel wall when it is viewed from outside.
    auto lift = [&](Vec3 p, Vec3 n, float d) { (void)n; (void)d; return p; };

    std::vector<Vertex> lines;
    const int rings_n = 17, spokes = 24, seg = 190;
    for (int k = 0; k <= rings_n; ++k) {
        double f = double(k) / rings_n;
        double r = rMin + (rMax - rMin) * f * f;
        for (int j = 0; j < seg; ++j) {
            double p0 = 2.0 * kerr::PI * j / seg, p1 = 2.0 * kerr::PI * (j + 1) / seg;
            Vec3 A, B, n, n2;
            surfacePoint(r, p0, a, A, n);
            surfacePoint(r, p1, a, B, n2);
            float al = 0.35f + 0.5f * (1.0f - (float)f);
            lines.push_back(mkv(lift(A, n, 0.05f), n, 0.50f, 0.88f, 1.0f, al));
            lines.push_back(mkv(lift(B, n2, 0.05f), n2, 0.50f, 0.88f, 1.0f, al));
        }
    }
    for (int s = 0; s < spokes; ++s) {
        double phi = 2.0 * kerr::PI * s / spokes;
        for (int k = 0; k < seg; ++k) {
            double f0 = double(k) / seg, f1 = double(k + 1) / seg;
            double r0 = rMin + (rMax - rMin) * f0 * f0;
            double r1 = rMin + (rMax - rMin) * f1 * f1;
            Vec3 A, B, n, n2;
            surfacePoint(r0, phi, a, A, n);
            surfacePoint(r1, phi, a, B, n2);
            float al = 0.35f + 0.5f * (1.0f - (float)f0);
            lines.push_back(mkv(lift(A, n, 0.05f), n, 0.50f, 0.88f, 1.0f, al));
            lines.push_back(mkv(lift(B, n2, 0.05f), n2, 0.50f, 0.88f, 1.0f, al));
        }
    }
    gridLines.upload(lines, GL_LINES);

    // ---- marker circles at the characteristic radii ----
    std::vector<Vertex> mk;
    auto ring = [&](double r, float cr, float cg, float cb) {
        if (r < rMin || r > rMax) return;
        const int N = 300;
        for (int j = 0; j < N; ++j) {
            double p0 = 2.0 * kerr::PI * j / N, p1 = 2.0 * kerr::PI * (j + 1) / N;
            Vec3 A, B, n, n2;
            surfacePoint(r, p0, a, A, n);
            surfacePoint(r, p1, a, B, n2);
            mk.push_back(mkv(lift(A, n, 0.14f), n, cr, cg, cb, 1.0f));
            mk.push_back(mkv(lift(B, n2, 0.14f), n2, cr, cg, cb, 1.0f));
        }
    };
    ring(kerr::horizonOuter(a) * 1.0002, 1.0f, 0.15f, 0.15f);   // event horizon
    ring(2.0,                            1.0f, 0.55f, 0.10f);   // equatorial ergosphere
    ring(kerr::photonRadius(a, true),    1.0f, 0.95f, 0.35f);   // prograde photon orbit
    ring(kerr::iscoRadius(a, true),      0.35f, 1.0f, 0.55f);   // ISCO
    rings.upload(mk, GL_LINES);
}

double funnelDepth() {
    if (!gProfile.built || gProfile.z.empty()) return 20.0;
    return std::abs(gProfile.z.front());
}

// -------------------------------------------------------- light cones ----

// Kerr-Schild metric restricted to the equatorial plane, in Cartesian (t,x,y).
// This chart is regular across the horizon, which is exactly what we need in
// order to draw cones there.
struct KSMetric { double tt, tx, ty, xx, xy, yy; };

static KSMetric ksEquatorial(double x, double y, double a) {
    double rho2 = x * x + y * y;
    // On the equator the defining equation rho^2/(r^2+a^2) = 1 gives r^2 = rho^2 - a^2,
    // and the Kerr-Schild scalar f = 2Mr^3/(r^4 + a^2 z^2) collapses to 2M/r.
    double r2 = std::max(rho2 - a * a, 1e-8);
    double r = std::sqrt(r2);
    double f = 2.0 / r;
    double lx = (r * x + a * y) / std::max(rho2, 1e-8);
    double ly = (r * y - a * x) / std::max(rho2, 1e-8);
    KSMetric g;
    g.tt = -1.0 + f;
    g.tx = f * lx;
    g.ty = f * ly;
    g.xx = 1.0 + f * lx * lx;
    g.xy = f * lx * ly;
    g.yy = 1.0 + f * ly * ly;
    return g;
}

void buildLightCones(Mesh& cones, Mesh& floorGrid, Mesh& markers,
                     double a, double rMax) {
    std::vector<Vertex> cv;
    Vec3 nrm(0, 0, 1);

    double rh = kerr::horizonOuter(a);
    double rhoH = std::sqrt(rh * rh + a * a);       // horizon in the KS chart
    double rhoE = std::sqrt(4.0 + a * a);           // equatorial ergosphere (r_BL = 2M)

    // Sample along a handful of radial spokes rather than a dense field: the
    // point of the picture is how a cone changes with r, and a thicket of
    // overlapping cones hides exactly that.
    // Uniform radial spacing and one shared time height, so every cone in the
    // picture is drawn to the same scale and can be compared directly.  The
    // cross-section is exactly linear in dt, so this is a fair comparison.
    const int NSPOKE = 4, NR = 9, NPSI = 64, NRIB = 16;
    double rho0 = 0.50 * rhoH, rho1 = rMax;
    double spacing = (rho1 - rho0) / double(NR - 1);
    double dt = 0.55 * spacing;

    std::vector<double> rhos(NR);
    for (int i = 0; i < NR; ++i) rhos[i] = rho0 + spacing * i;

    for (int ir = 0; ir < NR; ++ir) {
        double rho = rhos[ir];
        for (int is = 0; is < NSPOKE; ++is) {
            double phi = 2.0 * kerr::PI * is / NSPOKE;
            double x = rho * std::cos(phi), y = rho * std::sin(phi);
            KSMetric g = ksEquatorial(x, y, a);

            // Colour by the causal character of "staying put".
            float cr, cg, cb, al;
            if (rho < rhoH)      { cr = 1.00f; cg = 0.22f; cb = 0.24f; al = 1.00f; }
            else if (g.tt > 0.0) { cr = 1.00f; cg = 0.62f; cb = 0.14f; al = 0.95f; }
            else                 { cr = 0.38f; cg = 0.82f; cb = 1.00f; al = 0.80f; }

            Vec3 apex((float)x, (float)y, 0.0f);
            Vec3 prevRim; bool havePrev = false;
            int ribbed = 0;

            for (int k = 0; k <= NPSI; ++k) {
                double psi = 2.0 * kerr::PI * k / NPSI;
                double nx = std::cos(psi), ny = std::sin(psi);
                double B = g.tx * nx + g.ty * ny;
                double C = g.xx * nx * nx + 2.0 * g.xy * nx * ny + g.yy * ny * ny;
                double disc = B * B - C * g.tt;
                if (disc < 0.0 || C < 1e-9) { havePrev = false; continue; }
                // Null displacement of coordinate-time height dt in direction n.
                // Where no positive root exists the cone has tipped past that
                // direction entirely and the rim simply stops -- which is the
                // whole point of the picture.
                double s = dt * (-B + std::sqrt(disc)) / C;
                if (s <= 0.0) { havePrev = false; continue; }

                Vec3 rim((float)(x + s * nx), (float)(y + s * ny), (float)dt);
                if (havePrev) {
                    cv.push_back(mkv(prevRim, nrm, cr, cg, cb, al));
                    cv.push_back(mkv(rim,     nrm, cr, cg, cb, al));
                }
                if ((k % (NPSI / NRIB)) == 0) {
                    cv.push_back(mkv(apex, nrm, cr, cg, cb, al * 0.30f));
                    cv.push_back(mkv(rim,  nrm, cr, cg, cb, al * 0.75f));
                    ++ribbed;
                }
                prevRim = rim;
                havePrev = true;
            }
            // A short vertical stub marks "stay where you are".  Outside the
            // ergosphere it lies inside the cone; inside, it does not.
            cv.push_back(mkv(apex, nrm, 0.85f, 0.85f, 0.9f, 0.55f));
            cv.push_back(mkv(Vec3(apex.x, apex.y, (float)dt), nrm, 0.85f, 0.85f, 0.9f, 0.55f));
        }
    }
    cones.upload(cv, GL_LINES);

    // ---- polar reference grid on the t = 0 slice ----
    std::vector<Vertex> fg;
    Vec3 up(0, 0, 1);
    const int GRINGS = 14, GSPOKE = 24, GSEG = 200;
    for (int k = 1; k <= GRINGS; ++k) {
        double r = rMax * double(k) / GRINGS;
        for (int j = 0; j < GSEG; ++j) {
            double p0 = 2.0 * kerr::PI * j / GSEG, p1 = 2.0 * kerr::PI * (j + 1) / GSEG;
            fg.push_back(mkv(Vec3((float)(r * std::cos(p0)), (float)(r * std::sin(p0)), 0), up, 0.17f, 0.22f, 0.34f, 0.6f));
            fg.push_back(mkv(Vec3((float)(r * std::cos(p1)), (float)(r * std::sin(p1)), 0), up, 0.17f, 0.22f, 0.34f, 0.6f));
        }
    }
    for (int s = 0; s < GSPOKE; ++s) {
        double p = 2.0 * kerr::PI * s / GSPOKE;
        fg.push_back(mkv(Vec3((float)(rhoH * std::cos(p)), (float)(rhoH * std::sin(p)), 0), up, 0.17f, 0.22f, 0.34f, 0.6f));
        fg.push_back(mkv(Vec3((float)(rMax * std::cos(p)), (float)(rMax * std::sin(p)), 0), up, 0.17f, 0.22f, 0.34f, 0.6f));
    }
    floorGrid.upload(fg, GL_LINES);

    // ---- horizon and ergosphere circles ----
    std::vector<Vertex> mk;
    auto circle = [&](double R, float cr, float cg, float cb, float z) {
        const int N = 256;
        for (int j = 0; j < N; ++j) {
            double p0 = 2.0 * kerr::PI * j / N, p1 = 2.0 * kerr::PI * (j + 1) / N;
            mk.push_back(mkv(Vec3((float)(R * std::cos(p0)), (float)(R * std::sin(p0)), z), up, cr, cg, cb, 1.0f));
            mk.push_back(mkv(Vec3((float)(R * std::cos(p1)), (float)(R * std::sin(p1)), z), up, cr, cg, cb, 1.0f));
        }
    };
    circle(rhoH, 1.0f, 0.15f, 0.15f, 0.01f);
    circle(rhoE, 1.0f, 0.55f, 0.10f, 0.01f);
    circle(std::sqrt(kerr::iscoRadius(a, true) * kerr::iscoRadius(a, true) + a * a),
           0.35f, 1.0f, 0.55f, 0.01f);
    markers.upload(mk, GL_LINES);
}

// ---------------------------------------------------------- geodesics ----

// Integrate an equatorial geodesic and project it onto the embedding surface.
static Trajectory integrateEquatorial(double a, double E, double L, double r0,
                                      double pr0, double rMax, int maxSteps,
                                      float cr, float cg, float cb,
                                      double maxPhi = 1e30) {
    Trajectory tr;
    tr.r = cr; tr.g = cg; tr.b = cb;
    kerr::Constants c{a, E, L};
    kerr::State y{0.0, r0, kerr::PI / 2.0, 0.0, pr0, 0.0};
    double rh = kerr::horizonOuter(a);
    double h = 0.06;

    for (int i = 0; i < maxSteps; ++i) {
        kerr::State out;
        double e = kerr::rkck(y, c, h, out) / 1e-10;
        if (e > 1.0 && std::abs(h) > 1e-4) { h *= std::max(0.2, 0.9 * std::pow(e, -0.25)); continue; }
        y = out;
        h *= std::min(3.0, 0.9 * std::pow(std::max(e, 1e-6), -0.2));
        h = std::min(h, 0.25 * std::max(0.05, y.r - rh));

        if (y.r < rh * 1.002) { tr.captured = true; break; }
        if (y.r > rMax * 1.02) break;
        if (std::abs(y.ph) > maxPhi) break;

        Vec3 p, n;
        surfacePoint(y.r, y.ph, a, p, n);
        tr.pts.push_back(p);
    }
    return tr;
}

std::vector<Trajectory> buildTestParticles(double a, double rMax, int count) {
    std::vector<Trajectory> out;
    double rIsco = kerr::iscoRadius(a, true);

    for (int i = 0; i < count; ++i) {
        double f = double(i) / std::max(1, count - 1);

        if (i % 3 == 0) {
            // Stable circular orbit -- a closed ring, and a check that the
            // integrator reproduces exact circular motion.
            double rc = rIsco * 1.15 + f * (rMax * 0.55 - rIsco);
            double E, L;
            kerr::circularEL(rc, a, E, L, true);
            out.push_back(integrateEquatorial(a, E, L, rc, 0.0, rMax, 9000, 0.35f, 1.00f, 0.55f, 4.0 * kerr::PI));
        } else if (i % 3 == 1) {
            // Eccentric bound orbit -- shows relativistic periapsis precession.
            double rc = rIsco * 2.0 + f * (rMax * 0.5);
            double E, L;
            kerr::circularEL(rc, a, E, L, true);
            L *= 1.06;
            E = std::min(0.999, E * 1.004);
            double r0 = rc * 1.5;
            double Del = r0 * r0 - 2.0 * r0 + a * a;
            double P = (r0 * r0 + a * a) * E - a * L;
            double rad = (P * P / Del - (L - a * E) * (L - a * E) - r0 * r0) / Del;
            double pr = -std::sqrt(std::max(rad, 0.0));
            out.push_back(integrateEquatorial(a, E, L, r0, pr, rMax, 14000, 1.0f, 0.78f, 0.30f, 6.0 * kerr::PI));
        } else {
            // Plunge from rest at infinity: E = 1, modest angular momentum.
            double L = 0.8 + 1.6 * f;
            double E = 1.0;
            double r0 = rMax * 0.92;
            double Del = r0 * r0 - 2.0 * r0 + a * a;
            double P = (r0 * r0 + a * a) * E - a * L;
            double rad = (P * P / Del - (L - a * E) * (L - a * E) - r0 * r0) / Del;
            double pr = -std::sqrt(std::max(rad, 1e-6));
            out.push_back(integrateEquatorial(a, E, L, r0, pr, rMax, 9000, 1.0f, 0.30f, 0.40f, 8.0 * kerr::PI));
        }
    }
    return out;
}

std::vector<Trajectory> buildPhotonPaths(double a, double rMax, int count) {
    std::vector<Trajectory> out;
    // The critical impact parameter for a Schwarzschild hole is 3*sqrt(3) M;
    // sweep across it so both captured and deflected rays appear.
    for (int i = 0; i < count; ++i) {
        double f = double(i) / std::max(1, count - 1);
        double b = 1.0 + 11.0 * f;
        double E = 1.0, L = b;
        double r0 = rMax * 0.98;
        double Del = r0 * r0 - 2.0 * r0 + a * a;
        double P = (r0 * r0 + a * a) * E - a * L;
        double rad = (P * P / Del - (L - a * E) * (L - a * E)) / Del;
        if (rad <= 0.0) continue;
        double pr = -std::sqrt(rad);
        auto tr = integrateEquatorial(a, E, L, r0, pr, rMax, 12000, 1.0f, 0.88f, 0.25f);
        out.push_back(tr);
    }
    return out;
}

void uploadTrajectories(Mesh& mesh, const std::vector<Trajectory>& trs, float alpha) {
    std::vector<Vertex> v;
    Vec3 up(0, 0, 1);
    for (const auto& t : trs) {
        for (size_t i = 1; i < t.pts.size(); ++i) {
            float fade = alpha * (0.25f + 0.75f * float(i) / float(t.pts.size()));
            v.push_back(mkv(t.pts[i - 1], up, t.r, t.g, t.b, fade));
            v.push_back(mkv(t.pts[i],     up, t.r, t.g, t.b, fade));
        }
    }
    mesh.upload(v, GL_LINES);
}

}  // namespace geo
