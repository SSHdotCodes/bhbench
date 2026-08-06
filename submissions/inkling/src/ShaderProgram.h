#pragma once
#include <string>
#include <GL/glew.h>

class ShaderProgram {
public:
    ShaderProgram(const std::string& vertPath, const std::string& fragPath);
    ~ShaderProgram();
    void use() const;
    GLuint getProgram() const { return m_program; }
    void setMat4(const std::string& name, const float* value) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;
private:
    GLuint m_program;
    GLuint loadShader(const std::string& path, GLenum shaderType);
    std::string readFile(const std::string& path);
};
