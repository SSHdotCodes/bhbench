#include "Shader.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <vector>

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Shader: could not open file: " + path);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int Shader::compile(const std::string& source, unsigned int glShaderType, const std::string& debugName) {
    unsigned int shader = glCreateShader(glShaderType);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        std::string msg = "Shader compile error (" + debugName + "): " + std::string(log.data());
        glDeleteShader(shader);
        throw std::runtime_error(msg);
    }
    return shader;
}

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);

    unsigned int vs = compile(vertSrc, GL_VERTEX_SHADER, vertPath);
    unsigned int fs = 0;
    try {
        fs = compile(fragSrc, GL_FRAGMENT_SHADER, fragPath);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }

    programId = glCreateProgram();
    glAttachShader(programId, vs);
    glAttachShader(programId, fs);
    glLinkProgram(programId);

    int success = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &success);
    if (!success) {
        int len = 0;
        glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetProgramInfoLog(programId, len, nullptr, log.data());
        std::string msg = "Shader link error (" + vertPath + " / " + fragPath + "): " + std::string(log.data());
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(programId);
        programId = 0;
        throw std::runtime_error(msg);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader() {
    if (programId) glDeleteProgram(programId);
}

void Shader::use() const {
    glUseProgram(programId);
}

void Shader::setInt(const std::string& name, int v) const {
    glUniform1i(glGetUniformLocation(programId, name.c_str()), v);
}

void Shader::setFloat(const std::string& name, float v) const {
    glUniform1f(glGetUniformLocation(programId, name.c_str()), v);
}

void Shader::setVec3(const std::string& name, const glm::vec3& v) const {
    glUniform3f(glGetUniformLocation(programId, name.c_str()), v.x, v.y, v.z);
}

void Shader::setMat4(const std::string& name, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(programId, name.c_str()), 1, GL_FALSE, &v[0][0]);
}
