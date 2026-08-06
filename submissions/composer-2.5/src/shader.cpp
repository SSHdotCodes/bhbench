#include "shader.h"

#include "gl_loader.h"
#include <fstream>
#include <iostream>
#include <sstream>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string vertexCode = readFile(vertexPath);
    const std::string fragmentCode = readFile(fragmentPath);

    const unsigned int vertex = compileShader(GL_VERTEX_SHADER, vertexCode);
    const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode);
    program_ = linkProgram(vertex, fragment);

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setBool(const std::string& name, bool value) const {
    glUniform1i(uniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(uniformLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(uniformLocation(name), value);
}

void Shader::setVec2(const std::string& name, float x, float y) const {
    glUniform2f(uniformLocation(name), x, y);
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    glUniform3f(uniformLocation(name), x, y, z);
}

void Shader::setMat4(const std::string& name, const float* value) const {
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, value);
}

int Shader::uniformLocation(const std::string& name) const {
    const auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) {
        return it->second;
    }
    const int loc = glGetUniformLocation(program_, name.c_str());
    uniformCache_[name] = loc;
    return loc;
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int Shader::compileShader(unsigned int type, const std::string& source) {
    const unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader compile error:\n") + log);
    }
    return shader;
}

unsigned int Shader::linkProgram(unsigned int vertex, unsigned int fragment) {
    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader link error:\n") + log);
    }
    return program;
}