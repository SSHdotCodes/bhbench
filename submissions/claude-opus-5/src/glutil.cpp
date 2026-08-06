#include "glutil.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>

namespace gfx {

static std::time_t mtimeOf(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

static std::string dirOf(const std::string& p) {
    auto s = p.find_last_of('/');
    return (s == std::string::npos) ? std::string(".") : p.substr(0, s);
}

std::string readFileWithIncludes(const std::string& path, bool* ok) {
    std::ifstream f(path);
    if (!f) { if (ok) *ok = false; return ""; }
    if (ok) *ok = true;

    std::string dir = dirOf(path);
    std::ostringstream out;
    std::string line;
    while (std::getline(f, line)) {
        auto p = line.find("#include");
        if (p != std::string::npos) {
            auto q1 = line.find('"', p);
            auto q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                std::ifstream g(dir + "/" + inc);
                if (g) {
                    std::ostringstream ss;
                    ss << g.rdbuf();
                    out << ss.str() << "\n";
                    continue;
                }
            }
        }
        out << line << "\n";
    }
    return out.str();
}

static GLuint compileStage(GLenum type, const std::string& src, std::string& log) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, buf.data());
        log += (type == GL_VERTEX_SHADER ? "[vert] " : "[frag] ");
        log += buf.data();
        log += "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Program::build(std::string& log) {
    bool okv = false, okf = false;
    std::string vs = readFileWithIncludes(vp_, &okv);
    std::string fs = readFileWithIncludes(fp_, &okf);
    if (!okv) { log += "cannot open " + vp_ + "\n"; return false; }
    if (!okf) { log += "cannot open " + fp_ + "\n"; return false; }

    GLuint v = compileStage(GL_VERTEX_SHADER, vs, log);
    if (!v) return false;
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, log);
    if (!f) { glDeleteShader(v); return false; }

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(len > 1 ? len : 1);
        glGetProgramInfoLog(p, len, nullptr, buf.data());
        log += std::string("[link] ") + buf.data() + "\n";
        glDeleteProgram(p);
        return false;
    }
    if (id_) glDeleteProgram(id_);
    id_ = p;
    vt_ = mtimeOf(vp_);
    ft_ = mtimeOf(fp_);
    return true;
}

bool Program::load(const std::string& v, const std::string& f, std::string& log) {
    vp_ = v; fp_ = f;
    return build(log);
}

bool Program::reloadIfChanged(std::string& log) {
    if (mtimeOf(vp_) == vt_ && mtimeOf(fp_) == ft_) return false;
    // Touch the timestamps even on failure so we do not spin on a broken file.
    vt_ = mtimeOf(vp_); ft_ = mtimeOf(fp_);
    std::string tmp;
    if (!build(tmp)) { log = tmp; return true; }
    log.clear();
    return true;
}

GLint Program::loc(const char* n) const { return glGetUniformLocation(id_, n); }
void Program::set(const char* n, int v) const { glUniform1i(loc(n), v); }
void Program::set(const char* n, float v) const { glUniform1f(loc(n), v); }
void Program::set(const char* n, float x, float y) const { glUniform2f(loc(n), x, y); }
void Program::set(const char* n, float x, float y, float z) const { glUniform3f(loc(n), x, y, z); }
void Program::set(const char* n, float x, float y, float z, float w) const { glUniform4f(loc(n), x, y, z, w); }
void Program::setMat4(const char* n, const float* m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m); }

// ---------------------------------------------------------------------------

void RenderTarget::create(int width, int height, GLenum fmt, GLenum filter, bool withDepth) {
    destroy();
    w = width; h = height; format = fmt; wantDepth = withDepth;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLenum base = (fmt == GL_R32F || fmt == GL_R16F) ? GL_RED : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, base, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (withDepth) {
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "incomplete framebuffer %dx%d\n", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::resize(int width, int height) {
    if (width == w && height == h) return;
    GLint filt = GL_LINEAR;
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filt);
    create(width, height, format, (GLenum)filt, wantDepth);
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
}

void RenderTarget::destroy() {
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (tex) { glDeleteTextures(1, &tex); tex = 0; }
    if (depth) { glDeleteRenderbuffers(1, &depth); depth = 0; }
}

// ---------------------------------------------------------------------------

GLuint makeLUT1D(const std::vector<float>& rgb, int n) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, n, 1, 0, GL_RGB, GL_FLOAT, rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint makeLUT1D_R(const std::vector<float>& v, int n) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, n, 1, 0, GL_RED, GL_FLOAT, v.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void updateLUT1D_R(GLuint tex, const std::vector<float>& v, int n) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RED, GL_FLOAT, v.data());
}

GLuint fullscreenVAO() {
    static GLuint vao = 0;
    if (!vao) glGenVertexArrays(1, &vao);
    return vao;
}

void checkGL(const char* where) {
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR)
        std::fprintf(stderr, "GL error 0x%x at %s\n", e, where);
}

}  // namespace gfx
