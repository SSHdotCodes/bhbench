#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/gl.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
    unsigned int id;

    Shader(const std::string& vertPath, const std::string& fragPath) {
        std::ifstream vStream(vertPath), fStream(fragPath);
        if (!vStream.is_open() || !fStream.is_open()) {
            throw std::runtime_error("Shader file not found: " + vertPath + ", " + fragPath);
        }
        std::string vCode, fCode;
        std::stringstream vSS, fSS;
        vSS << vStream.rdbuf(); fSS << fStream.rdbuf();
        vCode = vSS.str(); fCode = fSS.str();

        const char* vSrc = vCode.c_str();
        const char* fSrc = fCode.c_str();

        unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vSrc, nullptr);
        glCompileShader(vert);
        checkCompileErrors(vert, "VERTEX");

        unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fSrc, nullptr);
        glCompileShader(frag);
        checkCompileErrors(frag, "FRAGMENT");

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);
        checkCompileErrors(id, "PROGRAM");

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() const { glUseProgram(id); }

    void setMat4(const std::string& name, glm::mat4 value) const {
        glUniformMatrix4fv(glGetUniformLocation(id, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
    }
    void setVec3(const std::string& name, float x, float y, float z) const {
        glUniform3f(glGetUniformLocation(id, name.c_str()), x, y, z);
    }
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(id, name.c_str()), value);
    }
    void setVec2(const std::string& name, float x, float y) const {
        glUniform2f(glGetUniformLocation(id, name.c_str()), x, y);
    }
    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(id, name.c_str()), value);
    }

private:
    static void checkCompileErrors(unsigned int shader, const char* type) {
        GLint success;
        GLchar infoLog[1024];
        if (type != std::string("PROGRAM")) {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(shader, 512, nullptr, infoLog);
                std::cerr << "Shader compile error (" << type << "):\n" << infoLog << "\n";
            }
        } else {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(shader, 512, nullptr, infoLog);
                std::cerr << "Shader link error (PROGRAM):\n" << infoLog << "\n";
            }
        }
    }
};
