#include "shaders.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace {

bool read_text(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

unsigned compile(unsigned type, const std::string& src, const char* path) {
    unsigned sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile failed (%s):\n%s\n", path, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

Shader::~Shader() {
    if (prog_) glDeleteProgram(prog_);
}

bool Shader::load(const std::string& vert_path, const std::string& frag_path) {
    std::string vs, fs;
    if (!read_text(vert_path, vs)) {
        std::fprintf(stderr, "cannot read %s\n", vert_path.c_str());
        return false;
    }
    if (!read_text(frag_path, fs)) {
        std::fprintf(stderr, "cannot read %s\n", frag_path.c_str());
        return false;
    }
    unsigned v = compile(GL_VERTEX_SHADER, vs, vert_path.c_str());
    unsigned f = compile(GL_FRAGMENT_SHADER, fs, frag_path.c_str());
    if (!v || !f) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, v);
    glAttachShader(prog_, f);
    glLinkProgram(prog_);
    glDeleteShader(v);
    glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        glDeleteProgram(prog_);
        prog_ = 0;
        return false;
    }
    return true;
}

void Shader::use() const { glUseProgram(prog_); }

int Shader::uniform(const char* name) const {
    return glGetUniformLocation(prog_, name);
}

std::string find_shader(const std::string& name) {
    // 1) shaders/ next to the working directory (run from project root),
    // 2) SHADER_DIR baked in at compile time (run from anywhere).
    {
        std::ifstream f("shaders/" + name);
        if (f) return "shaders/" + name;
    }
#ifdef SHADER_DIR
    return std::string(SHADER_DIR) + "/" + name;
#else
    return "shaders/" + name;
#endif
}
