#pragma once

#include <string>
#include <unordered_map>

// Minimal GLSL program wrapper (OpenGL 4.1 core).
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    bool loadCompute(const std::string& computePath); // unused on macOS GL 4.1, kept for clarity

    void use() const;
    unsigned int id() const { return program_; }

    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec3(const std::string& name, const float* v) const;
    void setMat4(const std::string& name, const float* m16) const;

private:
    unsigned int program_ = 0;
    mutable std::unordered_map<std::string, int> locationCache_;

    int location(const std::string& name) const;
    static unsigned int compile(unsigned int type, const std::string& source, std::string& err);
    static std::string readFile(const std::string& path);
};
