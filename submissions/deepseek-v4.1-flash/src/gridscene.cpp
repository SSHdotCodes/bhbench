#include "gridscene.h"

#include <cmath>
#include <cstdio>

#include "geodesic.h"
#include "physics.h"
#include "shaders_embedded.h"

namespace {

constexpr double kRs = bh::kRs;      // 2
constexpr double kRMax = 13.0;       // outer edge of the drawn grid
constexpr int kRings = 34;
constexpr int kSpokes = 56;
constexpr int kSegments = 72;

// Flamm paraboloid height.
inline double flammZ(double r) {
    if (r <= kRs) return 0.0;
    return 2.0 * std::sqrt(kRs * (r - kRs));
}

void ramp(double t, float& r, float& g, float& b) {
    struct Stop { double t; float r, g, b; };
    static const Stop stops[] = {
        {0.00, 0.80f, 1.00f, 1.00f},
        {0.14, 0.35f, 0.88f, 1.00f},
        {0.38, 0.28f, 0.58f, 1.00f},
        {0.66, 0.42f, 0.32f, 0.98f},
        {1.00, 0.26f, 0.11f, 0.52f},
    };
    const int n = sizeof(stops) / sizeof(stops[0]);
    if (t <= stops[0].t) { r = stops[0].r; g = stops[0].g; b = stops[0].b; return; }
    for (int i = 1; i < n; ++i) {
        if (t <= stops[i].t) {
            double u = (t - stops[i - 1].t) / (stops[i].t - stops[i - 1].t);
            r = float(stops[i - 1].r + (stops[i].r - stops[i - 1].r) * u);
            g = float(stops[i - 1].g + (stops[i].g - stops[i - 1].g) * u);
            b = float(stops[i - 1].b + (stops[i].b - stops[i - 1].b) * u);
            return;
        }
    }
    r = stops[n - 1].r; g = stops[n - 1].g; b = stops[n - 1].b;
}

inline double logT(double r) {
    return std::log(r / kRs) / std::log(kRMax / kRs);
}

}  // namespace

void GridScene::init() {
    program_ = glu::makeProgram(SHADER_GRID_VERT, SHADER_GRID_FRAG, "grid");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    buildStatic();
}

void GridScene::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void GridScene::buildStatic() {
    // ---- horizon: an opaque black disc filling the throat -------------------
    opaque_.clear();
    const int fan = 96;
    for (int i = 0; i < fan; ++i) {
        double a0 = 2.0 * M_PI * i / fan;
        double a1 = 2.0 * M_PI * (i + 1) / fan;
        opaque_.push_back({0.0f, 0.0f, -0.002f, 0, 0, 0, 1});
        opaque_.push_back({float(kRs * std::cos(a0)), float(kRs * std::sin(a0)), -0.002f, 0, 0, 0, 1});
        opaque_.push_back({float(kRs * std::cos(a1)), float(kRs * std::sin(a1)), -0.002f, 0, 0, 0, 1});
    }

    // ---- photon geodesics: a parallel bundle bent by the hole ---------------
    photonPaths_.clear();
    const double bList[] = {-11.0, -8.5, -7.0, -6.2, -5.7, -5.45, -5.30,
                            -5.22, -5.17, -5.10, -4.95, -4.6, -4.0, -3.0, -1.6,
                            0.0, 1.6, 3.0, 4.0, 4.6, 4.95, 5.10, 5.17, 5.22,
                            5.30, 5.45, 5.7, 6.2, 7.0, 8.5, 11.0};
    for (double b : bList) {
        m3::Vec3 start(-17.0, b, 0.03);
        m3::Vec3 vel(1.0, 0.0, 0.0);
        geo::Result res = geo::trace(start, vel, 60000, 17.0, 0.45, true);
        if (res.path.size() < 2) continue;

        Poly p;
        p.pts.reserve(res.path.size());
        for (auto& s : res.path) p.pts.push_back(s.pos);

        // Warm = captured, cool = escaped. Brightness peaks near b_c.
        double prox = std::exp(-std::pow((std::fabs(b) - bh::kBCrit) / 1.6, 2.0));
        if (res.captured) {
            p.r = 1.0f; p.g = 0.42f; p.b = 0.18f;
            p.weight = float(0.55 + 0.9 * prox);
        } else {
            p.r = 0.42f; p.g = 0.82f; p.b = 1.0f;
            p.weight = float(0.30 + 1.1 * prox);
        }
        photonPaths_.push_back(std::move(p));
    }

    // ---- massive test particles on plunging orbits --------------------------
    particlePaths_.clear();
    particlePhase_.clear();
    const int nParticles = 26;
    for (int i = 0; i < nParticles; ++i) {
        double r0 = 7.0 + 0.30 * i;
        double phi0 = i * 2.399963229728653;  // golden angle
        // Slightly sub-circular angular momentum -> guaranteed plunge.
        double h = 0.90 * r0 * std::sqrt(1.0 / (r0 - 3.0));
        m3::Vec3 pos(float(r0 * std::cos(phi0)), float(r0 * std::sin(phi0)), 0.0f);
        m3::Vec3 tan(-std::sin(phi0), std::cos(phi0), 0.0);
        m3::Vec3 rad(std::cos(phi0), std::sin(phi0), 0.0);
        m3::Vec3 vel = tan * float(h / r0) - rad * 0.03f;

        geo::Result res = geo::traceMassive(pos, vel, 40000, 20.0, 0.5, true);
        if (res.path.size() < 4) continue;

        Poly p;
        p.pts.reserve(res.path.size());
        for (auto& s : res.path) p.pts.push_back(s.pos);
        p.r = 1.0f; p.g = 0.72f; p.b = 0.35f;
        p.weight = 0.55f;
        particlePaths_.push_back(std::move(p));
        particlePhase_.push_back(float(i) / float(nParticles));
    }
}

void GridScene::buildDynamic(float time, float zScale) {
    lines_.clear();
    points_.clear();

    const double wave = 2.0 * time;

    // ---- Flamm paraboloid wireframe ----------------------------------------
    // Radial rings.
    for (int ri = 0; ri < kRings; ++ri) {
        double t = double(ri) / double(kRings - 1);
        double r = kRs * std::pow(kRMax / kRs, t);
        if (r < kRs * 1.004) r = kRs * 1.004;

        float cr, cg, cb;
        ramp(logT(r), cr, cg, cb);
        // Inward-travelling brightness wave: matter draining into the throat.
        float pulse = 0.62f + 0.38f * float(std::sin(4.2 * r + wave));
        float alpha = float(0.85 - 0.45 * logT(r)) * pulse;

        double z = zScale * (flammZ(r) + 0.10 * std::sin(3.0 * r - 1.6 * time) * std::exp(-r / 22.0));
        for (int s = 0; s < kSegments; ++s) {
            double a0 = 2.0 * M_PI * s / kSegments;
            double a1 = 2.0 * M_PI * (s + 1) / kSegments;
            lines_.push_back({float(r * std::cos(a0)), float(r * std::sin(a0)), float(z), cr, cg, cb, alpha});
            lines_.push_back({float(r * std::cos(a1)), float(r * std::sin(a1)), float(z), cr, cg, cb, alpha});
        }
    }

    // Radial spokes.
    const int spokeSteps = 64;
    for (int si = 0; si < kSpokes; ++si) {
        double a = 2.0 * M_PI * si / kSpokes;
        double ca = std::cos(a), sa = std::sin(a);
        for (int k = 0; k < spokeSteps; ++k) {
            double t0 = double(k) / spokeSteps;
            double t1 = double(k + 1) / spokeSteps;
            double r0 = kRs * std::pow(kRMax / kRs, t0);
            double r1 = kRs * std::pow(kRMax / kRs, t1);
            if (r0 < kRs * 1.004) r0 = kRs * 1.004;
            if (r1 < kRs * 1.004) r1 = kRs * 1.004;

            float c0r, c0g, c0b, c1r, c1g, c1b;
            ramp(logT(r0), c0r, c0g, c0b);
            ramp(logT(r1), c1r, c1g, c1b);
            float p0 = 0.62f + 0.38f * float(std::sin(4.2 * r0 + wave));
            float p1 = 0.62f + 0.38f * float(std::sin(4.2 * r1 + wave));
            float a0 = float(0.85 - 0.45 * logT(r0)) * p0;
            float a1 = float(0.85 - 0.45 * logT(r1)) * p1;

            double z0 = zScale * (flammZ(r0) + 0.10 * std::sin(3.0 * r0 - 1.6 * time) * std::exp(-r0 / 22.0));
            double z1 = zScale * (flammZ(r1) + 0.10 * std::sin(3.0 * r1 - 1.6 * time) * std::exp(-r1 / 22.0));

            lines_.push_back({float(r0 * ca), float(r0 * sa), float(z0), c0r, c0g, c0b, a0});
            lines_.push_back({float(r1 * ca), float(r1 * sa), float(z1), c1r, c1g, c1b, a1});
        }
    }

    // ---- glowing rim at the throat -----------------------------------------
    {
        const int seg = 160;
        float pulse = 0.75f + 0.25f * float(std::sin(3.0 * time));
        for (int s = 0; s < seg; ++s) {
            double a0 = 2.0 * M_PI * s / seg;
            double a1 = 2.0 * M_PI * (s + 1) / seg;
            lines_.push_back({float(kRs * std::cos(a0)), float(kRs * std::sin(a0)), 0.0f,
                              0.85f, 1.0f, 1.0f, 0.55f * pulse});
            lines_.push_back({float(kRs * std::cos(a1)), float(kRs * std::sin(a1)), 0.0f,
                              0.85f, 1.0f, 1.0f, 0.55f * pulse});
        }
    }

    // ---- photon geodesics ---------------------------------------------------
    for (auto& p : photonPaths_) {
        for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
            const m3::Vec3& a = p.pts[i];
            const m3::Vec3& b = p.pts[i + 1];
            // Fade the far ends so the bundle reads as light arriving.
            float fa = float(0.35 + 0.65 * std::exp(-std::fabs(a.x) / 13.0));
            float fb = float(0.35 + 0.65 * std::exp(-std::fabs(b.x) / 13.0));
            lines_.push_back({a.x, a.y, a.z, p.r, p.g, p.b, p.weight * fa});
            lines_.push_back({b.x, b.y, b.z, p.r, p.g, p.b, p.weight * fb});
        }
    }

    // ---- infalling particles ------------------------------------------------
    for (size_t pi = 0; pi < particlePaths_.size(); ++pi) {
        const Poly& p = particlePaths_[pi];
        size_t n = p.pts.size();
        if (n < 4) continue;

        // Faint orbit trail.
        for (size_t i = 0; i + 1 < n; i += 2) {
            const m3::Vec3& a = p.pts[i];
            const m3::Vec3& b = p.pts[i + 1];
            float f = float(1.0 - double(i) / double(n));
            lines_.push_back({a.x, a.y, a.z, p.r, p.g, p.b, 0.10f * f});
            lines_.push_back({b.x, b.y, b.z, p.r, p.g, p.b, 0.10f * f});
        }

        // Bright head travelling along the orbit.
        float phase = std::fmod(particlePhase_[pi] + time * 0.055f, 1.0f);
        size_t idx = size_t(phase * double(n - 1));
        for (int k = 0; k < 26; ++k) {
            size_t j = (idx >= size_t(k)) ? idx - k : 0;
            const m3::Vec3& q = p.pts[j];
            float f = 1.0f - float(k) / 26.0f;
            points_.push_back({q.x, q.y, q.z, 1.0f, 0.80f, 0.45f, f * f * 1.5f});
        }
    }
}

void GridScene::drawBuffer(const std::vector<V>& verts, GLenum mode, bool opaque,
                           bool roundPoints, float intensity, const m3::Mat4& mvp) {
    if (verts.empty()) return;

    glUseProgram(program_);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uMVP"), 1, GL_FALSE, mvp.data());
    glUniform1f(glGetUniformLocation(program_, "uIntensity"), intensity);
    glUniform1i(glGetUniformLocation(program_, "uRoundPoints"), roundPoints ? 1 : 0);
    glUniform1i(glGetUniformLocation(program_, "uOpaque"), opaque ? 1 : 0);
    glUniform1f(glGetUniformLocation(program_, "uPointSize"), 7.0f);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(V)),
                 verts.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

void GridScene::draw(const m3::Mat4& viewProj, float time, float intensity, float zScale) {
    buildDynamic(time, zScale);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Opaque horizon first so it occludes everything behind the throat.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    drawBuffer(opaque_, GL_TRIANGLES, true, false, 1.0f, viewProj);

    // Additive glow for the wireframe, geodesics and particles.
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    drawBuffer(lines_, GL_LINES, false, false, intensity, viewProj);
    drawBuffer(points_, GL_POINTS, false, true, intensity, viewProj);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
}
