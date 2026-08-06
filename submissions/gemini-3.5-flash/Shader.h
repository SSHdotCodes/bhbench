#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <unordered_map>

class Shader {
public:
    unsigned int ID;

    Shader(const char* vertexPath, const char* fragmentPath, bool isSource = false);
    ~Shader();

    void use();
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setMat4(const std::string& name, const float* matrix) const;

private:
    void checkCompileErrors(unsigned int shader, std::string type);
};

#endif
