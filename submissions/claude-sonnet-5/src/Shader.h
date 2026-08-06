#pragma once

#include <string>
#include <glm/glm.hpp>

// Loads, compiles and links a vertex+fragment GLSL program from disk and
// exposes typed uniform setters. OpenGL handles are stored as plain
// unsigned ints so this header doesn't need to pull in platform GL headers.
class Shader {
public:
    Shader(const std::string& vertPath, const std::string& fragPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;
    unsigned int id() const { return programId; }

    void setInt(const std::string& name, int v) const;
    void setFloat(const std::string& name, float v) const;
    void setVec3(const std::string& name, const glm::vec3& v) const;
    void setMat4(const std::string& name, const glm::mat4& v) const;

private:
    unsigned int programId = 0;

    static std::string readFile(const std::string& path);
    static unsigned int compile(const std::string& source, unsigned int glShaderType, const std::string& debugName);
};
