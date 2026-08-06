#pragma once
#include <string>
#include <iostream>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif
#include <glm/gtc/type_ptr.hpp>

inline GLuint compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetShaderInfoLog(shader, 2048, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << "\nSource:\n" << source.substr(0, 500) << std::endl;
    }
    return shader;
}

inline GLuint createProgram(const std::string& vertSrc, const std::string& fragSrc) {
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[2048];
        glGetProgramInfoLog(program, 2048, nullptr, infoLog);
        std::cerr << "Program linking failed:\n" << infoLog << std::endl;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

inline void setUniformMat4(GLuint prog, const char* name, const glm::mat4& mat) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
}
