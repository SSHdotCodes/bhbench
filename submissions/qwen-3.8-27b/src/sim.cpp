#include "sim.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bh {

static const float TWO_PI = 6.283185307179586f;

// ------------------------------------------------------------------ matrices
Mat4 matIdentity() {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 matPerspective(float fovyRad, float aspect, float znear, float zfar) {
    Mat4 r = matIdentity();
    float f = 1.0f / tanf(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    r.m[15] = 0.0f;
    return r;
}

Mat4 matLookAt(const float eye[3], const float center[3], const float up[3]) {
    auto sub = [](const float a[3], const float b[3]) { return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto cross = [](const float a[3], const float b[3]) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto norm = [](const float a[3]) {
        float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        return std::array<float, 3>{a[0] / l, a[1] / l, a[2] / l};
    };
    auto dot = [](const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };

    std::array<float, 3> fc = sub(eye, center);
    std::array<float, 3> z = norm(fc.data());
    std::array<float, 3> x = norm(cross(up, z.data()).data());
    std::array<float, 3> y = cross(z.data(), x.data());

    Mat4 r = matIdentity();
    r.m[0] = x[0]; r.m[4] = x[1]; r.m[8] = x[2];
    r.m[1] = y[0]; r.m[5] = y[1]; r.m[9] = y[2];
    r.m[2] = z[0]; r.m[6] = z[1]; r.m[10] = z[2];
    r.m[12] = -dot(x.data(), eye);
    r.m[13] = -dot(y.data(), eye);
    r.m[14] = -dot(z.data(), eye);
    r.m[15] = 1.0f;
    return r;
}

// ------------------------------------------------- photon geodesic particles
namespace {
inline void derivEq(float r, float dr, float dph, float E, float& ddr, float& ddph) {
    float f = std::max(1.0f - 2.0f / r, 0.03f);
    float invf3 = 1.0f / (f * f * f);
    ddr = invf3 * (E * E - dr * dr) / (r * r) - (r / f) * (dph * dph);
    ddph = dr * dph / r;
}

inline void stepEq(bh::Sim::Particle& p, float h) {
    struct S { float r, ph, dr, dph; };
    S s{p.r, p.ph, p.dr, p.dph};
    auto acc = [&](const S& q, float& dr, float& dph) {
        derivEq(q.r, q.dr, q.dph, p.E, dr, dph);
    };
    float a1r, a1p, a2r, a2p, a3r, a3p, a4r, a4p;
    acc(s, a1r, a1p);
    S s2{s.r + 0.5f * h * s.dr, s.ph + 0.5f * h * s.dph,
         s.dr + 0.5f * h * a1r, s.dph + 0.5f * h * a1p};
    acc(s2, a2r, a2p);
    S s3{s.r + 0.5f * h * s2.dr, s.ph + 0.5f * h * s2.dph,
         s.dr + 0.5f * h * a2r, s.dph + 0.5f * h * a2p};
    acc(s3, a3r, a3p);
    S s4{s.r + h * s3.dr, s.ph + h * s3.dph, s.dr + h * a3r, s.dph + h * a3p};
    acc(s4, a4r, a4p);
    p.r += (h / 6.0f) * (s.dr + 2.0f * s2.dr + 2.0f * s3.dr + s4.dr);
    p.ph += (h / 6.0f) * (s.dph + 2.0f * s2.dph + 2.0f * s3.dph + s4.dph);
    p.dr += (h / 6.0f) * (a1r + 2.0f * a2r + 2.0f * a3r + a4r);
    p.dph += (h / 6.0f) * (a1p + 2.0f * a2p + 2.0f * a3p + a4p);
}

void spawn(bh::Sim::Particle& p) {
    p.r = 55.0f + 35.0f * (float)std::rand() / 2147483647.0f;
    p.ph = TWO_PI * (float)std::rand() / 2147483647.0f;
    float sgn = (std::rand() & 1) ? 1.0f : -1.0f;
    float b;
    if ((float)std::rand() / 2147483647.0f < 0.70f) {
        // impact parameter clustered around the critical value 3*sqrt(3) ~ 5.196
        float u = 2.0f * (float)std::rand() / 2147483647.0f - 1.0f;
        b = sgn * 5.196f * std::fabs(tanhf(1.1f * u));
    } else {
        b = sgn * (7.0f + 15.0f * (float)std::rand() / 2147483647.0f);
    }
    float f0 = std::max(1.0f - 2.0f / p.r, 0.03f);
    p.E = std::sqrt(f0);
    float sa = b / p.r;
    float ca = std::sqrt(std::max(1.0f - sa * sa, 0.0f));
    p.dr = -ca * p.E;      // inward
    p.dph = sa / p.r;      // tangential
    p.b = b;
    p.age = 0.0f;
    p.alive = true;
    if (std::fabs(b) <= 5.196f) { p.cr = 1.0f; p.cg = 0.52f; p.cb = 0.16f; }
    else { p.cr = 0.30f; p.cg = 0.78f; p.cb = 1.0f; }
}

void kill(bh::Sim::Particle& p, float t) {
    p.alive = false;
    p.timer = 0.2f + 1.4f * (float)std::rand() / 2147483647.0f;
    p.deadT = t;
}
} // namespace

void Sim::init() {
    // funnel: radial rings from the throat (r=2) to r=70
    const int NR = 150, NPHI = 512, RMAX = 70;
    funnelV.clear();
    funnelI.clear();
    for (int i = 0; i <= NR; ++i) {
        float t = (float)i / NR;
        float r = 2.0f + (RMAX - 2.0f) * t;
        float z = funnelZ(r) * 0.55f;
        for (int j = 0; j < NPHI; ++j) {
            float ph = TWO_PI * (float)j / NPHI;
            funnelV.push_back(r * cosf(ph));
            funnelV.push_back(r * sinf(ph));
            funnelV.push_back(z);
            funnelV.push_back(r);
            funnelV.push_back(ph);
        }
    }
    for (int i = 0; i < NR; ++i) {
        for (int j = 0; j < NPHI; ++j) {
            unsigned int a = (unsigned int)(i * NPHI + j);
            unsigned int b = (unsigned int)(i * NPHI + (j + 1) % NPHI);
            unsigned int c = (unsigned int)((i + 1) * NPHI + j);
            unsigned int d = (unsigned int)((i + 1) * NPHI + (j + 1) % NPHI);
            funnelI.insert(funnelI.end(), {a, c, b, b, c, d});
        }
    }
    funnelNi = (int)funnelI.size();

    // cap: dark disk closing the throat (the "gate" of the horizon)
    const int NS = 96;
    capV.clear();
    capI.clear();
    capV.insert(capV.end(), {0.0f, 0.0f, 0.02f, 0.0f, 0.0f});
    for (int j = 0; j < NS; ++j) {
        float ph = TWO_PI * (float)j / NS;
        capV.push_back(2.4f * cosf(ph));
        capV.push_back(2.4f * sinf(ph));
        capV.push_back(0.02f);
        capV.push_back(2.4f);
        capV.push_back(ph);
    }
    for (int j = 0; j < NS; ++j) {
        capI.insert(capI.end(), {1u, (unsigned int)((j + 1) % NS), (unsigned int)j});
    }
    capNi = (int)capI.size();

    // trail segment indices (static topology)
    lineI.clear();
    for (int i = 0; i < NP; ++i) {
        for (int k = 0; k < TRAIL - 1; ++k) {
            lineI.push_back((unsigned int)(i * TRAIL + k));
            lineI.push_back((unsigned int)(i * TRAIL + k + 1));
        }
    }
    lineNi = (int)lineI.size();

    // particles
    std::srand(0xC0FFEEu);
    for (int i = 0; i < NP; ++i) {
        P[i].alive = false;
        P[i].timer = (float)i / NP * 1.5f;
        P[i].deadT = 0.0f;
        P[i].age = 0.0f;
        P[i].cr = P[i].cg = P[i].cb = 0.0f;
        P[i].r = 60.0f; P[i].ph = 0.0f; P[i].dr = -1.0f; P[i].dph = 0.0f;
        head[i] = 0;
        for (int k = 0; k < TRAIL; ++k) trail[i][k][0] = trail[i][k][1] = trail[i][k][2] = 0.0f;
    }
}

void Sim::setMode(int m) {
    mode = m;
    switch (m) {
        case 1: // lens view: ray-traced hole, no grid
            cam.yaw = 0.40f; cam.pitch = 0.22f; cam.dist = 42.0f;
            gridOn = false;
            break;
        case 2: // trapdoor view: high angle, grid + photons
            cam.yaw = 0.90f; cam.pitch = 1.02f; cam.dist = 105.0f;
            gridOn = true;
            break;
        default: // hybrid: ray-traced hole seen through the curved-space grid
            cam.yaw = 0.55f; cam.pitch = 0.45f; cam.dist = 62.0f;
            gridOn = true;
            break;
    }
}

void Sim::update(float dt) {
    if (paused) return;
    time += dt;
    for (int i = 0; i < NP; ++i) {
        Particle& p = P[i];
        if (!p.alive) {
            p.timer -= dt;
            if (p.timer <= 0.0f) spawn(p);
            continue;
        }
        p.age += dt;
        float adv = dt * 26.0f; // lambda units per frame
        int n = (int)std::ceil(adv / 0.05f);
        n = std::min(n, 48);
        float h = adv / (float)std::max(n, 1);
        for (int k = 0; k < n; ++k) {
            stepEq(p, h);
            if (p.r <= 2.02f) { kill(p, time); break; }          // swallowed
            if (p.r > 150.0f && p.dr > 0.0f) { kill(p, time); break; } // escaped
        }
        if (p.alive && p.age > 25.0f) kill(p, time);
        if (p.alive) {
            head[i] = (head[i] + 1) % TRAIL;
            trail[i][head[i]][0] = p.r * cosf(p.ph);
            trail[i][head[i]][1] = p.r * sinf(p.ph);
            trail[i][head[i]][2] = funnelZ(p.r) * 0.55f + 0.10f;
        }
    }
}

void Sim::buildLines(std::vector<float>& out) const {
    out.resize((size_t)NP * TRAIL * 7, 0.0f);
    for (int i = 0; i < NP; ++i) {
        const Particle& p = P[i];
        float deathFade = p.alive ? 1.0f : std::max(0.0f, 1.0f - (time - p.deadT) / 0.35f);
        float birthFade = std::min(1.0f, p.age * 3.0f);
        for (int k = 0; k < TRAIL; ++k) {
            int t = (head[i] - k + TRAIL) % TRAIL;
            float fade = 1.0f - (float)k / TRAIL;
            float a = 0.9f * fade * fade * deathFade * birthFade;
            float* o = out.data() + ((size_t)i * TRAIL + k) * 7;
            o[0] = trail[i][t][0];
            o[1] = trail[i][t][1];
            o[2] = trail[i][t][2];
            o[3] = p.cr * (0.35f + 0.65f * fade);
            o[4] = p.cg * (0.35f + 0.65f * fade);
            o[5] = p.cb * (0.35f + 0.65f * fade);
            o[6] = a;
        }
    }
}

std::string Sim::hudText(float fps, int iw, int ih) const {
    const char* modeName = (mode == 1) ? "lens" : (mode == 2) ? "trapdoor" : "hybrid";
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "Schwarzschild Black Hole - real-time geodesic ray tracer (Metal + MPS)\n"
                  "FPS %.0f   RES %.2fx   mode: %s   [G]rid [SPACE]ause [R]eset [A]uto-res\n"
                  "drag:orbit  wheel:zoom   1:lens  2:trapdoor  3:hybrid   [-/+]:res\n"
                  "horizon r=2 | photon sphere r=3 | ISCO r=6 | disk T(r)=1.5e7*(6/r)^0.75 K",
                  fps, resScale, modeName);
    return std::string(buf);
}

// ------------------------------------------------------------- HUD 5x7 font
// 95 glyphs (ASCII 32..126), 5 bits wide, 7 rows, MSB = leftmost pixel.
static const uint8_t FONT[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x08,0x08,0x08,0x08,0x08,0x00,0x08}, // !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // "
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // #
    {0x08,0x0F,0x14,0x0E,0x05,0x1E,0x08}, // $
    {0x06,0x0A,0x08,0x02,0x13,0x03,0x00}, // %
    {0x06,0x11,0x11,0x0D,0x15,0x11,0x0D}, // &
    {0x08,0x08,0x00,0x00,0x00,0x00,0x00}, // '
    {0x04,0x02,0x02,0x02,0x02,0x02,0x04}, // (
    {0x04,0x08,0x08,0x08,0x08,0x08,0x04}, // )
    {0x00,0x0A,0x15,0x1F,0x15,0x0A,0x00}, // *
    {0x00,0x08,0x08,0x1F,0x08,0x08,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x04,0x02}, // ,
    {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x06,0x06}, // .
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // /
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1E}, // 2
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1E,0x10,0x10,0x1E,0x01,0x01,0x1E}, // 5
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1E,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}, // 9
    {0x00,0x06,0x00,0x00,0x06,0x00,0x00}, // :
    {0x00,0x00,0x06,0x00,0x00,0x04,0x02}, // ;
    {0x01,0x02,0x04,0x08,0x04,0x02,0x01}, // <
    {0x00,0x00,0x1E,0x00,0x1E,0x00,0x00}, // =
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10}, // >
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x0E,0x11,0x1F,0x15,0x1F,0x0E,0x00}, // @
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1E,0x10,0x10,0x1E,0x10,0x10,0x1E}, // E
    {0x1E,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x06}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1E}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x17,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x08,0x08,0x04,0x08,0x08,0x0E}, // [
    {0x0E,0x02,0x02,0x04,0x02,0x02,0x0E}, // ]
    {0x04,0x0A,0x15,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // _
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // `
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0E}, // a
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // b
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // c
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0E}, // d
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // e
    {0x06,0x08,0x08,0x0E,0x08,0x08,0x08}, // f
    {0x00,0x00,0x0E,0x11,0x0F,0x01,0x0E}, // g
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // h
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // i
    {0x02,0x00,0x06,0x02,0x02,0x02,0x0C}, // j
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // k
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // l
    {0x00,0x00,0x1A,0x15,0x11,0x11,0x11}, // m
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // n
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // o
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // p
    {0x00,0x00,0x0E,0x11,0x0F,0x01,0x01}, // q
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // r
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, // s
    {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}, // t
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0E}, // u
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // v
    {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, // w
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // x
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // y
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // z
    {0x04,0x08,0x08,0x0A,0x08,0x08,0x04}, // {
    {0x08,0x08,0x08,0x08,0x08,0x08,0x08}, // |
    {0x04,0x02,0x02,0x05,0x02,0x02,0x04}, // }
    {0x00,0x00,0x00,0x0A,0x15,0x00,0x00}, // ~
};

void rasterHud(const std::string& text, int W, int H, std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)W * H * 4, 0);
    // translucent panel
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = 6;
        rgba[i + 1] = 10;
        rgba[i + 2] = 18;
        rgba[i + 3] = 150;
    }
    const int S = 2;   // supersampling scale
    const int CW = 12; // 5 px glyph + 1 gap, x2
    const int LH = 16; // 7 rows + padding, x2
    const int MX = 16, MY = 12;
    int line = 0, col = 0;
    for (char ch : text) {
        if (ch == '\n') { line++; col = 0; continue; }
        int c = (int)ch;
        if (c < 33 || c > 126) { col++; continue; }
        const uint8_t* g = FONT[c - 32];
        int x0 = MX + col * CW;
        int y0 = MY + line * LH;
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = g[row];
            for (int bit = 0; bit < 5; ++bit) {
                if (!(bits & (0x10u >> bit))) continue;
                for (int sy = 0; sy < S; ++sy) {
                    int py = y0 + row * S + sy;
                    if (py >= H) continue;
                    for (int sx = 0; sx < S; ++sx) {
                        int px = x0 + bit * S + sx;
                        if (px >= W) continue;
                        size_t o = ((size_t)py * W + px) * 4;
                        rgba[o + 0] = 205;
                        rgba[o + 1] = 235;
                        rgba[o + 2] = 255;
                        rgba[o + 3] = 240;
                    }
                }
            }
        }
        col++;
    }
}

} // namespace bh
