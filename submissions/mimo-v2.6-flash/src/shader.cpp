#include "shader.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace bh {

std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

namespace {
bool compile(GLenum type, const std::string& src, const std::string& name, GLuint& out) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(size_t(len) + 1, 0);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] compile failed (%s):\n%s\n", name.c_str(), log.data());
        glDeleteShader(s);
        return false;
    }
    out = s;
    return true;
}
}  // namespace

bool Program::loadSource(const std::string& name, const std::string& vs, const std::string& fs) {
    destroy();
    GLuint v = 0, f = 0;
    if (!compile(GL_VERTEX_SHADER, vs, name + ".vert", v)) return false;
    if (!compile(GL_FRAGMENT_SHADER, fs, name + ".frag", f)) {
        glDeleteShader(v);
        return false;
    }
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
        std::vector<char> log(size_t(len) + 1, 0);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] link failed (%s):\n%s\n", name.c_str(), log.data());
        glDeleteProgram(p);
        return false;
    }
    id_ = p;
    return true;
}

bool Program::load(const std::string& vertPath, const std::string& fragPath) {
    std::string vs = readTextFile(vertPath);
    std::string fs = readTextFile(fragPath);
    if (vs.empty() || fs.empty()) {
        std::fprintf(stderr, "[shader] cannot read %s / %s\n", vertPath.c_str(), fragPath.c_str());
        return false;
    }
    // Accept "#version" on the first line only; strip stray leading whitespace.
    size_t a = vs.find("#version"), b = fs.find("#version");
    if (a == std::string::npos || b == std::string::npos) {
        std::fprintf(stderr, "[shader] missing #version in %s / %s\n", vertPath.c_str(),
                     fragPath.c_str());
        return false;
    }
    vs = vs.substr(a);
    fs = fs.substr(b);
    return loadSource(vertPath, vs, fs);
}

void Program::destroy() {
    if (id_) glDeleteProgram(id_);
    id_ = 0;
    cache_.clear();
}

GLint Program::loc(const char* n) const {
    auto it = cache_.find(n);
    if (it != cache_.end()) return it->second;
    GLint l = glGetUniformLocation(id_, n);
    cache_[n] = l;
    return l;
}

void Program::set(const char* n, float v) const { glUniform1f(loc(n), v); }
void Program::set(const char* n, int v) const { glUniform1i(loc(n), v); }
void Program::set(const char* n, float a, float b) const { glUniform2f(loc(n), a, b); }
void Program::set(const char* n, float a, float b, float c) const { glUniform3f(loc(n), a, b, c); }
void Program::set(const char* n, const float* m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m); }

}  // namespace bh
