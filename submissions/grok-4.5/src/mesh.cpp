#include "mesh.hpp"

#include <cmath>
#include <utility>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_),
      indexCount_(other.indexCount_), lines_(other.lines_) {
    other.vao_ = other.vbo_ = other.ebo_ = 0;
    other.indexCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        indexCount_ = other.indexCount_;
        lines_ = other.lines_;
        other.vao_ = other.vbo_ = other.ebo_ = 0;
        other.indexCount_ = 0;
    }
    return *this;
}

void Mesh::destroy() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
}

void Mesh::build(const std::vector<float>& interleavedPosNormal,
                 const std::vector<unsigned int>& indices,
                 bool lines) {
    destroy();
    lines_ = lines;
    indexCount_ = indices.size();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(interleavedPosNormal.size() * sizeof(float)),
                 interleavedPosNormal.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    // layout: position (3) + normal (3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(lines_ ? GL_LINES : GL_TRIANGLES,
                   static_cast<GLsizei>(indexCount_),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// Flamm's paraboloid: embedding of the equatorial 2-geometry of Schwarzschild
// into R^3. For the spatial metric dl^2 = (1-rs/r)^{-1} dr^2 + r^2 dφ^2,
// the embedding z(r) satisfies 1 + (dz/dr)^2 = 1/(1-rs/r),
// hence z(r) = 2 √(rs (r - rs))  (taking the positive branch).
static inline float flammZ(float r, float rs) {
    if (r <= rs * 1.001f) return 0.0f;
    return 2.0f * std::sqrt(rs * (r - rs));
}

Mesh Mesh::createFlammSurface(float rs, float rMin, float rMax,
                              int nRadial, int nAzimuth) {
    rMin = std::max(rMin, rs * 1.02f);
    std::vector<float> verts;
    std::vector<unsigned int> idx;
    verts.reserve(static_cast<size_t>(nRadial * nAzimuth * 6));

    for (int j = 0; j < nRadial; ++j) {
        float tj = static_cast<float>(j) / static_cast<float>(nRadial - 1);
        float r = rMin + (rMax - rMin) * tj;
        float z = flammZ(r, rs);
        // analytic normal of surface of revolution z(r)
        float dzdr = (r > rs) ? std::sqrt(rs / (r - rs)) : 0.0f;
        // n ∝ (-dz/dr * ê_r + ê_z) then normalize in 3D after rotating

        for (int i = 0; i < nAzimuth; ++i) {
            float ti = static_cast<float>(i) / static_cast<float>(nAzimuth);
            float phi = static_cast<float>(2.0 * M_PI * ti);
            float c = std::cos(phi);
            float s = std::sin(phi);
            float x = r * c;
            float y = z; // vertical = embedding height (trapdoor)
            float zz = r * s;

            // Normal: radial slope
            float nx = -dzdr * c;
            float ny = 1.0f;
            float nz = -dzdr * s;
            float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz + 1e-12f);
            nx *= inv; ny *= inv; nz *= inv;

            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(zz);
            verts.push_back(nx);
            verts.push_back(ny);
            verts.push_back(nz);
        }
    }

    for (int j = 0; j < nRadial - 1; ++j) {
        for (int i = 0; i < nAzimuth; ++i) {
            int i1 = (i + 1) % nAzimuth;
            unsigned int a = static_cast<unsigned int>(j * nAzimuth + i);
            unsigned int b = static_cast<unsigned int>(j * nAzimuth + i1);
            unsigned int c = static_cast<unsigned int>((j + 1) * nAzimuth + i);
            unsigned int d = static_cast<unsigned int>((j + 1) * nAzimuth + i1);
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }

    // Mirror lower sheet for full "wormhole throat" / trapdoor look
    const size_t upperCount = verts.size() / 6;
    for (size_t k = 0; k < upperCount; ++k) {
        float x = verts[k * 6 + 0];
        float y = verts[k * 6 + 1];
        float z = verts[k * 6 + 2];
        float nx = verts[k * 6 + 3];
        float ny = verts[k * 6 + 4];
        float nz = verts[k * 6 + 5];
        verts.push_back(x);
        verts.push_back(-y);
        verts.push_back(z);
        verts.push_back(nx);
        verts.push_back(-ny);
        verts.push_back(nz);
    }
    const unsigned int base = static_cast<unsigned int>(upperCount);
    const size_t upperIdx = idx.size();
    for (size_t k = 0; k < upperIdx; k += 3) {
        // Flip winding for lower sheet
        idx.push_back(base + idx[k + 0]);
        idx.push_back(base + idx[k + 2]);
        idx.push_back(base + idx[k + 1]);
    }

    Mesh m;
    m.build(verts, idx, false);
    return m;
}

Mesh Mesh::createFlammGridLines(float rs, float rMin, float rMax,
                                int nRadial, int nAzimuth) {
    rMin = std::max(rMin, rs * 1.02f);
    std::vector<float> verts;
    std::vector<unsigned int> idx;

    auto addV = [&](float x, float y, float z) {
        unsigned int id = static_cast<unsigned int>(verts.size() / 6);
        verts.push_back(x);
        verts.push_back(y);
        verts.push_back(z);
        verts.push_back(0.0f);
        verts.push_back(1.0f);
        verts.push_back(0.0f);
        return id;
    };

    // Circles of constant r on upper and lower sheets
    for (int j = 0; j < nRadial; ++j) {
        float tj = static_cast<float>(j) / static_cast<float>(std::max(nRadial - 1, 1));
        float r = rMin + (rMax - rMin) * tj;
        float z = flammZ(r, rs);
        std::vector<unsigned int> ringU, ringL;
        ringU.reserve(static_cast<size_t>(nAzimuth));
        ringL.reserve(static_cast<size_t>(nAzimuth));
        for (int i = 0; i < nAzimuth; ++i) {
            float phi = static_cast<float>(2.0 * M_PI * i / nAzimuth);
            float x = r * std::cos(phi);
            float zz = r * std::sin(phi);
            ringU.push_back(addV(x, z, zz));
            ringL.push_back(addV(x, -z, zz));
        }
        for (int i = 0; i < nAzimuth; ++i) {
            int i1 = (i + 1) % nAzimuth;
            idx.push_back(ringU[i]); idx.push_back(ringU[i1]);
            idx.push_back(ringL[i]); idx.push_back(ringL[i1]);
        }
    }

    // Radial spokes
    const int nSpokes = nAzimuth / 2;
    for (int s = 0; s < nSpokes; ++s) {
        float phi = static_cast<float>(2.0 * M_PI * s / nSpokes);
        float c = std::cos(phi);
        float sn = std::sin(phi);
        unsigned int prevU = 0, prevL = 0;
        for (int j = 0; j < nRadial; ++j) {
            float tj = static_cast<float>(j) / static_cast<float>(std::max(nRadial - 1, 1));
            float r = rMin + (rMax - rMin) * tj;
            float z = flammZ(r, rs);
            unsigned int u = addV(r * c, z, r * sn);
            unsigned int l = addV(r * c, -z, r * sn);
            if (j > 0) {
                idx.push_back(prevU); idx.push_back(u);
                idx.push_back(prevL); idx.push_back(l);
            }
            prevU = u;
            prevL = l;
        }
    }

    Mesh m;
    m.build(verts, idx, true);
    return m;
}

Mesh Mesh::createCurvatureCage(float rs, float halfExtent, int divisions) {
    // Draw a planar polar grid in the equatorial plane, with vertices displaced
    // vertically by Flamm embedding so the "flat" coordinate grid falls into
    // the trapdoor throat as r → rs.
    std::vector<float> verts;
    std::vector<unsigned int> idx;

    auto addV = [&](float x, float y, float z) {
        unsigned int id = static_cast<unsigned int>(verts.size() / 6);
        verts.push_back(x); verts.push_back(y); verts.push_back(z);
        verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f);
        return id;
    };

    const int nR = divisions;
    const int nA = divisions * 2;
    const float rMax = halfExtent;
    const float rMin = rs * 1.05f;

    for (int j = 0; j <= nR; ++j) {
        float tj = static_cast<float>(j) / static_cast<float>(nR);
        float r = rMin + (rMax - rMin) * tj;
        float z = flammZ(r, rs) * 0.35f; // scale for aesthetic cage height
        std::vector<unsigned int> ring;
        for (int i = 0; i < nA; ++i) {
            float phi = static_cast<float>(2.0 * M_PI * i / nA);
            ring.push_back(addV(r * std::cos(phi), -z, r * std::sin(phi)));
        }
        for (int i = 0; i < nA; ++i) {
            idx.push_back(ring[i]);
            idx.push_back(ring[(i + 1) % nA]);
        }
    }

    for (int i = 0; i < nA; i += 2) {
        float phi = static_cast<float>(2.0 * M_PI * i / nA);
        float c = std::cos(phi), s = std::sin(phi);
        unsigned int prev = 0;
        for (int j = 0; j <= nR; ++j) {
            float tj = static_cast<float>(j) / static_cast<float>(nR);
            float r = rMin + (rMax - rMin) * tj;
            float z = flammZ(r, rs) * 0.35f;
            unsigned int id = addV(r * c, -z, r * s);
            if (j > 0) {
                idx.push_back(prev);
                idx.push_back(id);
            }
            prev = id;
        }
    }

    Mesh m;
    m.build(verts, idx, true);
    return m;
}
