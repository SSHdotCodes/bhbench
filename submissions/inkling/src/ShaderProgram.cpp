#include "ShaderProgram.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string ShaderProgram::readFile(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint ShaderProgram::loadShader(const std::string& path, GLenum shaderType) {
    std::string srcStr = readFile(path);
    const char* src = srcStr.c_str();
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compile error in " << path << ":\n" << infoLog << std::endl;
    }
    return shader;
}

ShaderProgram::ShaderProgram(const std::string& vertPath, const std::string& fragPath)
    : m_program(0) {
    GLuint vs = loadShader(vertPath, GL_VERTEX_SHADER);
    GLuint fs = loadShader(fragPath, GL_FRAGMENT_SHADER);
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    GLint linkSuccess;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linkSuccess);
    if (!linkSuccess) {
        char infoLog[512];
        glGetProgramInfoLog(m_program, 512, nullptr, infoLog);
        std::cerr << "Shader link error: " << infoLog << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

ShaderProgram::~ShaderProgram() {
    glDeleteProgram(m_program);
}

void ShaderProgram::use() const { glUseProgram(m_program); }
void ShaderProgram::setMat4(const std::string& name, const float* value) const {
    glUniformMatrix4fv(glGetUniformLocation(m_program, name.c_str()), 1, GL_FALSE, value);
}
void ShaderProgram::setVec3(const std::string& name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(m_program, name.c_str()), x, y, z);
}
void ShaderProgram::setFloat(const std::string& name, float value) const {
    glUniform1f(glGetUniformLocation(m_program, name.c_str()), value);
}
void ShaderProgram::setInt(const std::string& name, int value) const {
    glUniform1i(glGetUniformLocation(m_program, name.c_str()), value);
}
