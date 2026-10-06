#pragma once
#define GLEW_NO_GLU
#include <GL/glew.h>
#include <cstdio>
#include <string>

inline void checkGL(const char* where) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        std::fprintf(stderr, "GL error 0x%x at %s\n", e, where);
    }
}

inline GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error (%s):\n%s\n",
                     type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        return 0;
    }
    return s;
}

inline GLuint buildProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

struct Texture2D {
    GLuint id = 0;
    int w = 0, h = 0;
};

inline Texture2D makeTexture(int w, int h, GLenum internal = GL_RGBA16F,
                             GLenum format = GL_RGBA, GLenum type = GL_FLOAT) {
    Texture2D t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

struct FBO {
    GLuint fbo = 0;
    Texture2D tex;
};

inline FBO makeFBO(int w, int h) {
    FBO f;
    f.tex = makeTexture(w, h);
    glGenFramebuffers(1, &f.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.tex.id, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "FBO incomplete (%dx%d)\n", w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return f;
}

inline void savePPM(const std::string& path, int w, int h, const unsigned char* rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
}
