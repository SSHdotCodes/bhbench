// =============================================================================
//  gl_util.hpp — thin OpenGL 4.1 helpers: shader programs, render targets,
//  4x4 matrices, PNG output.
// =============================================================================
#pragma once

#define GL_SILENCE_DEPRECATION
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "shaders_embedded.hpp"

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace glu {

// ---------------------------------------------------------------------------
// Shader sources: embedded in the executable; set BH_SHADER_DIR to load them
// from disk instead (handy while editing shaders).
// ---------------------------------------------------------------------------
inline std::string rawSource(const std::string& name) {
    if (const char* dir = std::getenv("BH_SHADER_DIR")) {
        std::ifstream f(std::string(dir) + "/" + name);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    for (const EmbeddedShader& s : kEmbeddedShaders)
        if (name == s.name) return s.source;
    std::fprintf(stderr, "shader source '%s' not found\n", name.c_str());
    std::exit(1);
}

// Resolves  #include "file"  lines.
inline std::string shaderSource(const std::string& name) {
    std::istringstream in(rawSource(name));
    std::string out, line;
    while (std::getline(in, line)) {
        size_t p = line.find("#include");
        if (p != std::string::npos && line.find_first_not_of(" \t") == p) {
            size_t a = line.find('"'), b = line.rfind('"');
            if (a != std::string::npos && b > a) {
                out += shaderSource(line.substr(a + 1, b - a - 1));
                out += '\n';
                continue;
            }
        }
        out += line;
        out += '\n';
    }
    return out;
}

inline GLuint compile(GLenum type, const std::string& name) {
    std::string src = shaderSource(name);
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader compile error in %s:\n%s\n", name.c_str(), log);
        std::exit(1);
    }
    return s;
}

struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;

    GLint loc(const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, name);
        cache.emplace(name, l);
        return l;
    }
    void use() const { glUseProgram(id); }
    void set(const char* n, int v) { glUniform1i(loc(n), v); }
    void set(const char* n, float v) { glUniform1f(loc(n), v); }
    void set(const char* n, double v) { glUniform1f(loc(n), float(v)); }
    void set(const char* n, float x, float y) { glUniform2f(loc(n), x, y); }
    void set(const char* n, float x, float y, float z) { glUniform3f(loc(n), x, y, z); }
    void set(const char* n, float x, float y, float z, float w) { glUniform4f(loc(n), x, y, z, w); }
    void setMat4(const char* n, const float* m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m); }
};

inline Program makeProgram(const char* vsName, const char* fsName) {
    Program p;
    GLuint vs = compile(GL_VERTEX_SHADER, vsName), fs = compile(GL_FRAGMENT_SHADER, fsName);
    p.id = glCreateProgram();
    glAttachShader(p.id, vs);
    glAttachShader(p.id, fs);
    glLinkProgram(p.id);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p.id, sizeof log, nullptr, log);
        std::fprintf(stderr, "program link error (%s + %s):\n%s\n", vsName, fsName, log);
        std::exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// ---------------------------------------------------------------------------
// Textures and render targets
// ---------------------------------------------------------------------------
inline GLuint makeTexture2D(int w, int h, GLenum internal, GLenum format, GLenum type, const void* data,
                            GLenum filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

struct Target {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    void destroy() {
        if (tex) glDeleteTextures(1, &tex);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        fbo = tex = 0;
    }
    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
    }
};

inline void checkFramebuffer(const char* what) {
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "framebuffer '%s' incomplete (0x%x)\n", what, st);
        std::exit(1);
    }
}

inline Target makeTarget(int w, int h, GLenum internal, GLenum filter = GL_LINEAR) {
    Target t;
    t.w = w;
    t.h = h;
    t.tex = makeTexture2D(w, h, internal, GL_RGBA, GL_FLOAT, nullptr, filter);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    checkFramebuffer("target");
    return t;
}

inline void bindTexture(int unit, GLenum target, GLuint tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, tex);
}

// ---------------------------------------------------------------------------
// Column-major 4x4 matrices
// ---------------------------------------------------------------------------
struct Mat4 { float m[16]; };

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
inline Mat4 perspective(double fovY, double aspect, double zn, double zf) {
    double f = 1.0 / std::tan(fovY * 0.5);
    Mat4 r{};
    r.m[0] = float(f / aspect);
    r.m[5] = float(f);
    r.m[10] = float((zf + zn) / (zn - zf));
    r.m[11] = -1.0f;
    r.m[14] = float(2.0 * zf * zn / (zn - zf));
    return r;
}
// View matrix from an orthonormal camera frame (right, up, forward) at eye.
inline Mat4 viewMatrix(const double eye[3], const double right[3], const double up[3], const double fwd[3]) {
    Mat4 r{};
    for (int i = 0; i < 3; ++i) {
        r.m[i * 4 + 0] = float(right[i]);
        r.m[i * 4 + 1] = float(up[i]);
        r.m[i * 4 + 2] = float(-fwd[i]);
    }
    r.m[12] = float(-(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]));
    r.m[13] = float(-(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]));
    r.m[14] = float(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
    r.m[15] = 1.0f;
    return r;
}

// ---------------------------------------------------------------------------
// PNG writer (8-bit RGB, rows top to bottom) using zlib.
// ---------------------------------------------------------------------------
inline bool writePNG(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + size_t(y) * w * 3, rgb.begin() + size_t(y + 1) * w * 3);
    }
    uLongf clen = compressBound(uLong(raw.size()));
    std::vector<unsigned char> comp(clen);
    if (compress2(comp.data(), &clen, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    comp.resize(clen);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [](unsigned char* p, unsigned v) {
        p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
        p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
    };
    auto chunk = [&](const char* tag, const unsigned char* data, unsigned len) {
        unsigned char b[4];
        be32(b, len);
        std::fwrite(b, 1, 4, f);
        std::fwrite(tag, 1, 4, f);
        if (len) std::fwrite(data, 1, len, f);
        uLong c = crc32(0L, (const Bytef*)tag, 4);
        if (len) c = crc32(c, data, len);
        be32(b, unsigned(c));
        std::fwrite(b, 1, 4, f);
    };
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::fwrite(sig, 1, 8, f);
    unsigned char ihdr[13];
    be32(ihdr, unsigned(w));
    be32(ihdr + 4, unsigned(h));
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", comp.data(), unsigned(comp.size()));
    chunk("IEND", nullptr, 0);
    std::fclose(f);
    return true;
}

}  // namespace glu
