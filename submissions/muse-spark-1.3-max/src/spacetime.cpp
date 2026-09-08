#include "spacetime.hpp"

#include <cmath>
#include <cstdio>

#include <glm/gtc/type_ptr.hpp>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include "physics.hpp"

namespace {

constexpr double RIM_R = 22.0;   // funnel rim radius (M)
constexpr double WSCALE = 0.55;  // vertical compression of the embedding
constexpr double THROAT_R = 2.002;

double rim_w() { return bh::flamm_w(RIM_R); }

// CPU twin of the shader's blackbody ramp (0 = cool .. 1 = hot).
void blackbody(float x, float& r, float& g, float& b) {
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    float c2[3] = {1.0f, 0.42f, 0.07f};
    float c3[3] = {1.0f, 0.90f, 0.74f};
    float c4[3] = {0.62f, 0.78f, 1.0f};
    float c1[3] = {0.42f, 0.04f, 0.0f};
    auto mix3 = [&](float* a, float* bb, float t, float* o) {
        for (int i = 0; i < 3; ++i) o[i] = a[i] + (bb[i] - a[i]) * t;
    };
    auto smooth = [&](float a, float bb, float v) {
        float t = (v - a) / (bb - a);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return t * t * (3 - 2 * t);
    };
    float o[3], tmp[3];
    mix3(c1, c2, smooth(0.0f, 0.38f, x), tmp);
    mix3(tmp, c3, smooth(0.38f, 0.72f, x), o);
    mix3(o, c4, smooth(0.72f, 1.0f, x), tmp);
    r = tmp[0];
    g = tmp[1];
    b = tmp[2];
}

// Deterministic PRNG (mulberry32) so the scene is stable run to run.
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed) {}
    double next() {
        s += 0x6D2B79F5;
        std::uint32_t t = s;
        t = (t ^ (t >> 15)) * (t | 1);
        t ^= t + ((t ^ (t >> 7)) * (t | 61));
        return ((t ^ (t >> 14)) >> 0) / 4294967296.0;
    }
};

}  // namespace

double SpacetimeScene::surf_y(double r) {
    if (r < THROAT_R) r = THROAT_R;
    return (bh::flamm_w(r) - rim_w()) * WSCALE;
}

void SpacetimeScene::upload(Mesh& m, const std::vector<Vertex>& v,
                            const std::vector<unsigned>& idx, unsigned mode,
                            bool dynamic) {
    destroy(m);
    m.mode = mode;
    m.dynamic = dynamic;
    m.indexed = !idx.empty();
    m.count = m.indexed ? (int)idx.size() : (int)v.size();
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(),
                 dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(6 * sizeof(float)));
    if (m.indexed) {
        glGenBuffers(1, &m.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned),
                     idx.data(), GL_STATIC_DRAW);
    }
    glBindVertexArray(0);
}

void SpacetimeScene::draw_mesh(const Mesh& m) {
    if (!m.count) return;
    glBindVertexArray(m.vao);
    if (m.indexed)
        glDrawElements(m.mode, m.count, GL_UNSIGNED_INT, nullptr);
    else
        glDrawArrays(m.mode, 0, m.count);
    glBindVertexArray(0);
}

void SpacetimeScene::destroy(Mesh& m) {
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m = Mesh();
}

void SpacetimeScene::set_mode(int unlit, int glow, float alpha, float ptsize) {
    glUniform1i(u_unlit_, unlit);
    glUniform1i(u_glow_, glow);
    glUniform1f(u_alpha_, alpha);
    glUniform1f(u_ptsize_, ptsize);
}

bool SpacetimeScene::init(double spin) {
    spin_ = spin;
    if (!prog_.load(find_shader("grid.vert"), find_shader("grid.frag")))
        return false;
    u_mvp_ = prog_.uniform("uMVP");
    u_model_ = prog_.uniform("uModel");
    u_cam_ = prog_.uniform("uCamPos");
    u_light_ = prog_.uniform("uLightDir");
    u_unlit_ = prog_.uniform("uUnlit");
    u_glow_ = prog_.uniform("uGlow");
    u_alpha_ = prog_.uniform("uAlpha");
    u_ptsize_ = prog_.uniform("uPointSize");
    build_static();
    build_disk_and_isco();
    build_rays();
    build_particles();
    return true;
}

void SpacetimeScene::set_spin(double spin) {
    spin_ = spin;
    build_disk_and_isco();
}

void SpacetimeScene::build_static() {
    // ---- Flamm paraboloid surface (lit) ----
    {
        const int NR = 72, NS = 128;
        std::vector<Vertex> v;
        std::vector<unsigned> idx;
        v.reserve((NR + 1) * (NS + 1));
        const double l0 = std::log(THROAT_R), l1 = std::log(RIM_R);
        for (int i = 0; i <= NR; ++i) {
            const double r = std::exp(l0 + (l1 - l0) * i / NR);
            const double y = surf_y(r);
            // Profile slope dy/dr (analytic derivative of Flamm w).
            const double dydr =
                std::sqrt(bh::RS / (r - bh::RS)) * WSCALE;
            // Surface-of-revolution normal: radial part -dydr, up part 1.
            const double nl = std::sqrt(dydr * dydr + 1.0);
            const double nr = -dydr / nl, ny = 1.0 / nl;
            // Depth tint: deep indigo throat -> slate-blue rim.
            const float t = (float)i / NR;
            const float cr = 0.06f + 0.16f * t;
            const float cg = 0.05f + 0.30f * t;
            const float cb = 0.16f + 0.44f * t;
            for (int j = 0; j <= NS; ++j) {
                const double ph = 2.0 * bh::PI * j / NS;
                const float cx = (float)std::cos(ph),
                            sx = (float)std::sin(ph);
                v.push_back({(float)(r * cx), (float)y, (float)(r * sx),
                             (float)(nr * cx), (float)ny, (float)(nr * sx),
                             cr, cg, cb, 1.0f});
            }
        }
        for (int i = 0; i < NR; ++i)
            for (int j = 0; j < NS; ++j) {
                const unsigned a = i * (NS + 1) + j;
                const unsigned b = a + NS + 1;
                idx.push_back(a);
                idx.push_back(b);
                idx.push_back(a + 1);
                idx.push_back(a + 1);
                idx.push_back(b);
                idx.push_back(b + 1);
            }
        upload(funnel_, v, idx, GL_TRIANGLES);
    }
    // ---- Funnel grid lines (unlit cyan) ----
    {
        std::vector<Vertex> v;
        auto line = [&](double x0, double y0, double z0, double x1, double y1,
                        double z1, float a) {
            v.push_back({(float)x0, (float)y0, (float)z0, 0, 1, 0, 0.30f,
                         0.75f, 1.0f, a});
            v.push_back({(float)x1, (float)y1, (float)z1, 0, 1, 0, 0.30f,
                         0.75f, 1.0f, a});
        };
        const double l0 = std::log(THROAT_R), l1 = std::log(RIM_R);
        for (int i = 0; i <= 24; ++i) {  // parallels
            const double r = std::exp(l0 + (l1 - l0) * i / 24);
            const double y = surf_y(r) + 0.03;
            for (int j = 0; j < 96; ++j) {
                const double p0 = 2 * bh::PI * j / 96;
                const double p1 = 2 * bh::PI * (j + 1) / 96;
                line(r * std::cos(p0), y, r * std::sin(p0),
                     r * std::cos(p1), y, r * std::sin(p1), 0.45f);
            }
        }
        for (int j = 0; j < 24; ++j) {  // meridians
            const double ph = 2 * bh::PI * j / 24;
            const double cx = std::cos(ph), sx = std::sin(ph);
            double pr = THROAT_R, py = surf_y(THROAT_R) + 0.03;
            for (int i = 1; i <= 48; ++i) {
                const double r = std::exp(l0 + (l1 - l0) * i / 48);
                const double y = surf_y(r) + 0.03;
                line(pr * cx, py, pr * sx, r * cx, y, r * sx, 0.30f);
                pr = r;
                py = y;
            }
        }
        upload(funnel_lines_, v, {}, GL_LINES);
    }
    // ---- Flat reference grid at rim level (the "undisturbed" plane) ----
    {
        std::vector<Vertex> v;
        auto seg = [&](double x0, double z0, double x1, double z1) {
            const double mx = 0.5 * (x0 + x1), mz = 0.5 * (z0 + z1);
            if (std::sqrt(mx * mx + mz * mz) < RIM_R + 0.5) return;
            v.push_back({(float)x0, 0, (float)z0, 0, 1, 0, 0.20f, 0.32f,
                         0.48f, 0.55f});
            v.push_back({(float)x1, 0, (float)z1, 0, 1, 0, 0.20f, 0.32f,
                         0.48f, 0.55f});
        };
        for (int i = -15; i <= 15; ++i) {
            seg(i * 3.0, -45.0, i * 3.0, 45.0);
            seg(-45.0, i * 3.0, 45.0, i * 3.0);
        }
        upload(flat_grid_, v, {}, GL_LINES);
    }
    // ---- Horizon sphere (near-black) + glow shell, plugging the throat ----
    {
        const double cy = surf_y(THROAT_R) + 0.35;
        auto sphere = [&](double rad, float cr, float cg, float cb,
                          float a) {
            std::vector<Vertex> v;
            std::vector<unsigned> idx;
            const int NL = 40, NM = 56;
            for (int i = 0; i <= NL; ++i) {
                const double th = bh::PI * i / NL;
                for (int j = 0; j <= NM; ++j) {
                    const double ph = 2 * bh::PI * j / NM;
                    const float nx = (float)(std::sin(th) * std::cos(ph));
                    const float ny = (float)std::cos(th);
                    const float nz = (float)(std::sin(th) * std::sin(ph));
                    v.push_back({(float)(rad * nx), (float)(cy + rad * ny),
                                 (float)(rad * nz), nx, ny, nz, cr, cg, cb,
                                 a});
                }
            }
            for (int i = 0; i < NL; ++i)
                for (int j = 0; j < NM; ++j) {
                    const unsigned a0 = i * (NM + 1) + j;
                    const unsigned b0 = a0 + NM + 1;
                    idx.push_back(a0);
                    idx.push_back(b0);
                    idx.push_back(a0 + 1);
                    idx.push_back(a0 + 1);
                    idx.push_back(b0);
                    idx.push_back(b0 + 1);
                }
            return std::make_pair(v, idx);
        };
        auto h = sphere(bh::RS, 0.004f, 0.004f, 0.010f, 1.0f);
        upload(horizon_, h.first, h.second, GL_TRIANGLES);
        auto g = sphere(bh::RS * 1.45, 1.0f, 0.55f, 0.22f, 0.55f);
        upload(glow_shell_, g.first, g.second, GL_TRIANGLES);
    }
    // ---- Photon ring (r = 3M circle draped on the funnel) ----
    {
        std::vector<Vertex> v;
        const int N = 180;
        for (int j = 0; j < N; ++j) {
            for (int k = 0; k <= 1; ++k) {
                const double p = 2 * bh::PI * (j + k) / N;
                const double y = surf_y(bh::R_PHOTON) + 0.05;
                v.push_back({(float)(bh::R_PHOTON * std::cos(p)), (float)y,
                             (float)(bh::R_PHOTON * std::sin(p)), 0, 1, 0,
                             1.0f, 0.72f, 0.25f, 0.95f});
            }
        }
        upload(photon_ring_, v, {}, GL_LINES);
    }
}

void SpacetimeScene::build_disk_and_isco() {
    const double r_in = bh::disk_inner(spin_);
    const double r_out = bh::DISK_OUTER;
    // ---- Accretion disk draped on the funnel (temperature colors) ----
    {
        const int NR = 40, NS = 160;
        std::vector<Vertex> v;
        std::vector<unsigned> idx;
        const double rp = bh::disk_flux_peak_r(r_in);
        const double fp = bh::disk_flux(rp, r_in);
        for (int i = 0; i <= NR; ++i) {
            const double r = r_in + (r_out - r_in) * i / NR;
            const double y = surf_y(r) + 0.07;
            const double Tn =
                std::pow(bh::disk_flux(r, r_in) / fp, 0.25);
            float cr, cg, cb;
            blackbody((float)std::pow(Tn / 1.25, 0.65), cr, cg, cb);
            const float I = (float)(0.25 + 2.2 * std::pow(Tn, 3.0));
            const float edge =
                (float)(i == 0 || i == NR ? 0.35 : 1.0);
            for (int j = 0; j <= NS; ++j) {
                const double ph = 2 * bh::PI * j / NS;
                v.push_back({(float)(r * std::cos(ph)), (float)y,
                             (float)(r * std::sin(ph)), 0, 1, 0,
                             cr * I * edge, cg * I * edge, cb * I * edge,
                             0.85f});
            }
        }
        for (int i = 0; i < NR; ++i)
            for (int j = 0; j < NS; ++j) {
                const unsigned a = i * (NS + 1) + j;
                const unsigned b = a + NS + 1;
                idx.push_back(a);
                idx.push_back(b);
                idx.push_back(a + 1);
                idx.push_back(a + 1);
                idx.push_back(b);
                idx.push_back(b + 1);
            }
        upload(disk_, v, idx, GL_TRIANGLES);
    }
    // ---- ISCO ring (spin-dependent) ----
    {
        std::vector<Vertex> v;
        const int N = 180;
        for (int j = 0; j < N; ++j) {
            for (int k = 0; k <= 1; ++k) {
                const double p = 2 * bh::PI * (j + k) / N;
                const double y = surf_y(r_in) + 0.06;
                v.push_back({(float)(r_in * std::cos(p)), (float)y,
                             (float)(r_in * std::sin(p)), 0, 1, 0, 1.0f,
                             0.42f, 0.10f, 0.95f});
            }
        }
        upload(isco_ring_, v, {}, GL_LINES);
    }
}

void SpacetimeScene::build_rays() {
    // Bent null geodesics from the CPU integrator, projected onto the
    // embedding (y follows the funnel). Cyan = escapes, ember = captured.
    std::vector<Vertex> v;
    const double bs[] = {2.7, 3.4, 4.2, 5.0, 5.8, 6.8, 8.2, 10.0};
    for (double b : bs) {
        for (int side = -1; side <= 1; side += 2) {
            bh::TraceResult tr =
                bh::trace_world(bh::Vec3(-34, 0, side * b), bh::Vec3(1, 0, 0),
                                true, 40000);
            const float cr = tr.captured ? 1.0f : 0.30f;
            const float cg = tr.captured ? 0.30f : 0.90f;
            const float cb = tr.captured ? 0.12f : 1.00f;
            auto emit = [&](const bh::Vec3& p) {
                const double r = p.len();
                const double y = (r < RIM_R ? surf_y(r) : 0.0) + 0.14;
                v.push_back({(float)p.x, (float)y, (float)p.z, 0, 1, 0, cr,
                             cg, cb, 0.9f});
            };
            for (size_t i = 1; i < tr.path.size(); ++i) {
                emit(tr.path[i - 1]);
                emit(tr.path[i]);
            }
        }
    }
    upload(rays_, v, {}, GL_LINES);
}

void SpacetimeScene::build_particles() {
    particles_.clear();
    Rng rng(1337);
    // Circular orbiters between disk edges.
    for (int i = 0; i < 200; ++i) {
        const double r = 6.5 + rng.next() * 9.5;
        Particle p;
        p.a_semi = r;
        p.ecc = 0;
        p.phi = rng.next() * 2 * bh::PI;
        p.pomega = 0;
        p.omega = bh::omega_kerr(r, spin_);
        p.prec_rate = 0;
        p.plunge = false;
        const float w = (float)(0.55 + rng.next() * 0.45);
        p.cr = w;
        p.cg = w * (float)(0.82 + rng.next() * 0.12);
        p.cb = w * (float)(0.62 + rng.next() * 0.15);
        particles_.push_back(p);
    }
    // A few eccentric ones showing GR perihelion precession.
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.a_semi = 11.0 + i * 1.5;
        p.ecc = 0.35;
        p.phi = rng.next() * 2 * bh::PI;
        p.pomega = 0;
        p.omega = bh::omega_kerr(p.a_semi, spin_);
        // Weak-field precession per orbit 6πM/a(1-e^2), illustrative here.
        p.prec_rate = p.omega * (6 * bh::PI * bh::MASS /
                                 (p.a_semi * (1 - p.ecc * p.ecc))) /
                      (2 * bh::PI);
        p.plunge = false;
        p.cr = 0.5f;
        p.cg = 1.0f;
        p.cb = 0.6f;
        particles_.push_back(p);
    }
    // Plungers: spiral from r=9 down the throat, then respawn.
    for (int i = 0; i < 12; ++i) {
        Particle p;
        p.a_semi = 9.0;
        p.ecc = 0;
        p.phi = rng.next() * 2 * bh::PI;
        p.pomega = -rng.next() * 3.0;  // staggered start (reuse as timer)
        p.omega = bh::omega_kerr(7.0, spin_) * 2.0;
        p.prec_rate = 0;
        p.plunge = true;
        p.cr = 1.0f;
        p.cg = 0.45f;
        p.cb = 0.15f;
        particles_.push_back(p);
    }
    point_verts_.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i)
        point_verts_[i] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 1};
    upload(points_, point_verts_, {}, GL_POINTS, true);
    update(0.0);
}

void SpacetimeScene::update(double sim_time) {
    // Illustrative time rate shared with the disk pattern.
    const double t = sim_time * 6.0;
    for (size_t i = 0; i < particles_.size(); ++i) {
        Particle& p = particles_[i];
        double r, ph;
        if (p.plunge) {
            double lt = t * 0.25 + p.pomega;  // slow fall, staggered
            double cyc = lt - std::floor(lt / 4.0) * 4.0;
            r = 9.0 * std::exp(-cyc * 0.55) + 0.05;
            if (r < THROAT_R + 0.05) r = THROAT_R + 0.05;
            ph = p.phi + t * p.omega * (1.0 + 3.0 / r);
        } else if (p.ecc > 0) {
            const double n = t * p.omega;
            p.pomega = t * p.prec_rate;
            const double nu = n - p.pomega;
            r = p.a_semi * (1 - p.ecc * p.ecc) / (1 + p.ecc * std::cos(nu));
            ph = p.phi + p.pomega + nu;
        } else {
            r = p.a_semi;
            ph = p.phi + t * p.omega;
        }
        const double y = (r < RIM_R ? surf_y(r) : 0.0) + 0.16;
        Vertex& v = point_verts_[i];
        v.px = (float)(r * std::cos(ph));
        v.py = (float)y;
        v.pz = (float)(r * std::sin(ph));
        v.nx = 0;
        v.ny = 1;
        v.nz = 0;
        v.r = p.cr;
        v.g = p.cg;
        v.b = p.cb;
        v.a = 1.0f;
    }
    glBindBuffer(GL_ARRAY_BUFFER, points_.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, point_verts_.size() * sizeof(Vertex),
                    point_verts_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SpacetimeScene::render(const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec3& cam_pos) {
    prog_.use();
    glm::mat4 mvp = proj * view;
    glm::mat4 model(1.0f);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3f(u_cam_, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform3f(u_light_, 0.4f, 1.0f, 0.3f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Opaque pass. Polygon offset keeps the draped lines/disk/rings
    // (drawn just above the funnel) from z-fighting at far zoom.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    set_mode(0, 0, 1.0f, 0.0f);
    draw_mesh(funnel_);
    glDisable(GL_POLYGON_OFFSET_FILL);
    set_mode(1, 0, 1.0f, 0.0f);
    draw_mesh(horizon_);

    // Transparent pass (no depth writes).
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    set_mode(1, 0, 1.0f, 0.0f);
    draw_mesh(funnel_lines_);
    draw_mesh(flat_grid_);
    draw_mesh(photon_ring_);
    draw_mesh(isco_ring_);
    draw_mesh(rays_);
    // Additive glow pass.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    set_mode(1, 0, 0.9f, 0.0f);
    draw_mesh(disk_);
    glDisable(GL_CULL_FACE);
    // Depth-tested: the funnel walls correctly occlude the throat glow.
    set_mode(1, 1, 1.0f, 0.0f);
    draw_mesh(glow_shell_);
    set_mode(1, 0, 1.0f, 110.0f);
    draw_mesh(points_);
    // Restore default state.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}
