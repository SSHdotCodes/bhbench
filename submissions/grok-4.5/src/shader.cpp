#include "shader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

Shader::~Shader() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

Shader::Shader(Shader&& other) noexcept
    : program_(other.program_), locationCache_(std::move(other.locationCache_)) {
    other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_) glDeleteProgram(program_);
        program_ = other.program_;
        locationCache_ = std::move(other.locationCache_);
        other.program_ = 0;
    }
    return *this;
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open shader file: " << path << "\n";
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

unsigned int Shader::compile(unsigned int type, const std::string& source, std::string& err) {
    unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        int len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1));
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        err = log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    const std::string vsrc = readFile(vertPath);
    const std::string fsrc = readFile(fragPath);
    if (vsrc.empty() || fsrc.empty()) return false;

    std::string err;
    unsigned int vs = compile(GL_VERTEX_SHADER, vsrc, err);
    if (!vs) {
        std::cerr << "Vertex shader compile error (" << vertPath << "):\n" << err << "\n";
        return false;
    }
    unsigned int fs = compile(GL_FRAGMENT_SHADER, fsrc, err);
    if (!fs) {
        std::cerr << "Fragment shader compile error (" << fragPath << "):\n" << err << "\n";
        glDeleteShader(vs);
        return false;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        int len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1));
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "Shader link error:\n" << log.data() << "\n";
        glDeleteProgram(prog);
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = prog;
    locationCache_.clear();
    return true;
}

bool Shader::loadCompute(const std::string&) {
    std::cerr << "Compute shaders require OpenGL 4.3+ (not available on macOS GL 4.1).\n";
    return false;
}

void Shader::use() const {
    glUseProgram(program_);
}

int Shader::location(const std::string& name) const {
    auto it = locationCache_.find(name);
    if (it != locationCache_.end()) return it->second;
    int loc = glGetUniformLocation(program_, name.c_str());
    locationCache_[name] = loc;
    return loc;
}

void Shader::setBool(const std::string& name, bool value) const {
    glUniform1i(location(name), value ? 1 : 0);
}
void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(location(name), value);
}
void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(location(name), value);
}
void Shader::setVec2(const std::string& name, float x, float y) const {
    glUniform2f(location(name), x, y);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    glUniform3f(location(name), x, y, z);
}
void Shader::setVec3(const std::string& name, const float* v) const {
    glUniform3fv(location(name), 1, v);
}
void Shader::setMat4(const std::string& name, const float* m16) const {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, m16);
}
