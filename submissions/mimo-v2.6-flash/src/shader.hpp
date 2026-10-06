// OpenGL shader program wrapper: loads GLSL from disk, links, caches uniforms.
#pragma once

#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <GL/glew.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace bh {

class Program {
public:
    Program() = default;
    ~Program() { destroy(); }
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    bool load(const std::string& vertPath, const std::string& fragPath);
    bool loadSource(const std::string& name, const std::string& vs, const std::string& fs);
    void destroy();

    void use() const { glUseProgram(id_); }
    GLuint id() const { return id_; }
    bool valid() const { return id_ != 0; }

    void set(const char* n, float v) const;
    void set(const char* n, int v) const;
    void set(const char* n, float a, float b) const;
    void set(const char* n, float a, float b, float c) const;
    void set(const char* n, const float* m16) const;
    void set1i(const char* n, int v) const { set(n, v); }

private:
    GLint loc(const char* n) const;
    GLuint id_ = 0;
    mutable std::unordered_map<std::string, GLint> cache_;
};

// Reads a text file; returns empty string on failure.
std::string readTextFile(const std::string& path);

}  // namespace bh
