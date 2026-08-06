#pragma once

#include <string>
#include <unordered_map>

class Shader {
public:
    Shader() = default;
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    void use() const;
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setMat4(const std::string& name, const float* value) const;

    unsigned int id() const { return program_; }

private:
    unsigned int program_ = 0;
    mutable std::unordered_map<std::string, int> uniformCache_;

    int uniformLocation(const std::string& name) const;
    static std::string readFile(const std::string& path);
    static unsigned int compileShader(unsigned int type, const std::string& source);
    static unsigned int linkProgram(unsigned int vertex, unsigned int fragment);
};