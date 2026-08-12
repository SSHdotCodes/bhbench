#pragma once

#include "gl_compat.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

inline std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline GLuint compileShader(GLenum type, const std::string& src, const std::string& name) {
    const GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len) + 1, 0);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile failed (" << name << "):\n" << log.data() << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

struct Shader {
    GLuint id = 0;

    static Shader fromSources(const std::string& vs, const std::string& fs,
                              const std::string& vsName, const std::string& fsName) {
        Shader out;
        const GLuint v = compileShader(GL_VERTEX_SHADER, vs, vsName);
        const GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, fsName);
        if (!v || !f) {
            if (v) glDeleteShader(v);
            if (f) glDeleteShader(f);
            return out;
        }
        out.id = glCreateProgram();
        glAttachShader(out.id, v);
        glAttachShader(out.id, f);
        glLinkProgram(out.id);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(out.id, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetProgramiv(out.id, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(static_cast<size_t>(len) + 1, 0);
            glGetProgramInfoLog(out.id, len, nullptr, log.data());
            std::cerr << "Program link failed (" << vsName << " + " << fsName << "):\n"
                      << log.data() << std::endl;
            glDeleteProgram(out.id);
            out.id = 0;
        }
        return out;
    }

    static Shader fromFiles(const std::string& vsPath, const std::string& fsPath) {
        const std::string vs = readTextFile(vsPath);
        const std::string fs = readTextFile(fsPath);
        if (vs.empty() || fs.empty()) {
            std::cerr << "Failed to read shaders:\n  " << vsPath << "\n  " << fsPath << std::endl;
            return {};
        }
        return fromSources(vs, fs, vsPath, fsPath);
    }

    void use() const { glUseProgram(id); }

    GLint loc(const char* name) const { return glGetUniformLocation(id, name); }

    void setInt(const char* n, int v) const { glUniform1i(loc(n), v); }
    void setFloat(const char* n, float v) const { glUniform1f(loc(n), v); }
    void setVec2(const char* n, float x, float y) const { glUniform2f(loc(n), x, y); }
    void setVec3(const char* n, float x, float y, float z) const { glUniform3f(loc(n), x, y, z); }
    void setMat4(const char* n, const float* m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m); }

    void destroy() {
        if (id) {
            glDeleteProgram(id);
            id = 0;
        }
    }
};
