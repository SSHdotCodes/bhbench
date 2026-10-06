#pragma once
// Pure C++ core: camera math, Flamm-paraboloid mesh, photon-geodesic
// particle system, and the HUD bitmap-font rasterizer. No Obj-C / Metal here.

#include <string>
#include <vector>
#include <cstdint>

namespace bh {

// ------------------------------------------------------------------ matrices
// Column-major 4x4, Metal-compatible (v' = M * v, column vectors, OpenGL style).
struct Mat4 { float m[16]; };

Mat4 matIdentity();
Mat4 matPerspective(float fovyRad, float aspect, float znear, float zfar);
Mat4 matLookAt(const float eye[3], const float center[3], const float up[3]);

// ------------------------------------------------------------- camera orbits
struct Camera {
    float yaw = 0.40f;
    float pitch = 0.22f;   // radians above the equatorial plane
    float dist = 42.0f;    // distance from the hole (M units)

    void position(float p[3]) const {
        float cp = cosf(pitch);
        p[0] = dist * cp * cosf(yaw);
        p[1] = dist * cp * sinf(yaw);
        p[2] = dist * sinf(pitch);
    }
};

// Flamm paraboloid height (M = 1): z^2 = 8 (r - 2)
inline float funnelZ(float r) {
    float x = r - 2.0f;
    if (x < 1e-4f) x = 1e-4f;
    return 2.0f * sqrtf(2.0f * x);
}

// ------------------------------------------------------------------- the sim
struct Sim {
    // flags
    bool paused = false;
    bool gridOn = false;    // show the spacetime trapdoor grid
    bool autoRes = true;    // adaptive internal resolution
    bool hudOn = true;
    float resScale = 0.5f;  // internal resolution factor
    float time = 0.0f;
    int mode = 1;           // 1 = lens, 2 = trapdoor, 3 = hybrid

    Camera cam;

    // funnel (trapdoor) mesh
    std::vector<float> funnelV;          // px,py,pz,r,ph
    std::vector<unsigned int> funnelI;
    int funnelNi = 0;
    // event-horizon cap disk
    std::vector<float> capV;
    std::vector<unsigned int> capI;
    int capNi = 0;
    // static trail index buffer (particle i trail: segments k..k+1 of slot i*TRAIL)
    std::vector<unsigned int> lineI;
    int lineNi = 0;

    // photons animated along equatorial geodesics (drawn on the funnel)
    struct Particle {
        float r, ph, dr, dph;   // state (theta = pi/2, L_theta = 0)
        float E, b;             // conserved energy, impact parameter
        float age, timer, deadT;
        bool alive;
        float cr, cg, cb;
    };
    static constexpr int NP = 128;
    static constexpr int TRAIL = 40;
    Particle P[NP];
    float trail[NP][TRAIL][3];
    int head[NP];

    void init();
    void setMode(int m);
    void update(float dt);
    // packs NP*TRAIL vertices of (px,py,pz, cx,cy,cz, a) for the line pass
    void buildLines(std::vector<float>& out) const;
    std::string hudText(float fps, int iw, int ih) const;
};

// 5x7 bitmap font -> RGBA8 (panel + text)
void rasterHud(const std::string& text, int W, int H, std::vector<uint8_t>& rgba);

} // namespace bh
