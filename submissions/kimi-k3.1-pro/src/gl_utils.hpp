#pragma once

#include <OpenGL/gl3.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

inline std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

inline GLuint compileShader(GLenum type, const std::string& source,
                            const std::string& label) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char log[8192];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(label + " compilation failed:\n" + log);
    }
    return shader;
}

inline GLuint makeProgram(const std::string& vertexSource,
                          const std::string& fragmentSource,
                          const std::string& label = "shader") {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, label + " vertex");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, label + " fragment");
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        char log[8192];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(label + " link failed:\n" + log);
    }
    return program;
}

inline GLuint loadProgram(const std::string& directory,
                          const std::string& vertexName,
                          const std::string& fragmentName) {
    return makeProgram(readTextFile(directory + "/" + vertexName),
                       readTextFile(directory + "/" + fragmentName),
                       vertexName + "/" + fragmentName);
}

inline GLint uniform(GLuint program, const char* name) {
    return glGetUniformLocation(program, name);
}
