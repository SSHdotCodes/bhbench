// Spacetime-curvature view: Flamm's paraboloid embedding of the
// Schwarzschild equatorial plane ("trapdoor in spacetime"), with a flat
// reference grid, draped accretion disk, photon/ISCO rings, CPU-traced
// bent light rays, and test particles (incl. plunging + precessing ones).
#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "shaders.hpp"

class SpacetimeScene {
  public:
    bool init(double spin);
    void set_spin(double spin);
    void update(double sim_time);  // advance test particles
    void render(const glm::mat4& view, const glm::mat4& proj,
                const glm::vec3& cam_pos);

    // Embedding surface height: rim (r=22M) meets the flat grid at y = 0,
    // throat (r -> rs) dips below. Vertical scale compressed 0.55x.
    static double surf_y(double r);

  private:
    struct Mesh {
        unsigned vao = 0, vbo = 0, ebo = 0;
        int count = 0;
        unsigned mode = 0x0004;  // GL_TRIANGLES
        bool indexed = false;
        bool dynamic = false;
    };
    struct Vertex {
        float px, py, pz, nx, ny, nz, r, g, b, a;
    };
    struct Particle {
        double a_semi, ecc, phi, pomega, omega, prec_rate;
        bool plunge;
        float cr, cg, cb;
    };

    Shader prog_;
    int u_mvp_ = -1, u_model_ = -1, u_cam_ = -1, u_light_ = -1,
        u_unlit_ = -1, u_glow_ = -1, u_alpha_ = -1, u_ptsize_ = -1;

    Mesh funnel_, funnel_lines_, flat_grid_, horizon_, glow_shell_,
        photon_ring_, isco_ring_, disk_, rays_, points_;
    std::vector<Particle> particles_;
    std::vector<Vertex> point_verts_;
    double spin_ = 0.0;

    void upload(Mesh& m, const std::vector<Vertex>& v,
                const std::vector<unsigned>& idx, unsigned mode,
                bool dynamic = false);
    void draw_mesh(const Mesh& m);
    void destroy(Mesh& m);
    void set_mode(int unlit, int glow, float alpha, float ptsize);
    void build_static();
    void build_disk_and_isco();
    void build_rays();
    void build_particles();
};
