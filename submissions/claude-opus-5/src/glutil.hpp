// glutil.hpp — thin OpenGL 4.1 helpers: shader hot-reloading, float render
// targets, and 1-D lookup textures.

#pragma once
#include <OpenGL/gl3.h>
#include <string>
#include <vector>
#include <ctime>

namespace gfx {

// A shader program built from files on disk.  `reloadIfChanged` recompiles when
// any source file's mtime moves, so shaders can be edited while the sim runs.
class Program {
public:
    bool load(const std::string& vertPath, const std::string& fragPath, std::string& log);
    bool reloadIfChanged(std::string& log);
    void use() const { glUseProgram(id_); }
    GLuint id() const { return id_; }
    GLint  loc(const char* name) const;
    bool   valid() const { return id_ != 0; }

    void set(const char* n, int v) const;
    void set(const char* n, float v) const;
    void set(const char* n, float x, float y) const;
    void set(const char* n, float x, float y, float z) const;
    void set(const char* n, float x, float y, float z, float w) const;
    void setMat4(const char* n, const float* m) const;

private:
    GLuint id_ = 0;
    std::string vp_, fp_;
    std::time_t vt_ = 0, ft_ = 0;
    bool build(std::string& log);
};

// Colour render target.  `format` is a sized internal format, e.g. GL_RGBA16F.
struct RenderTarget {
    GLuint fbo = 0, tex = 0, depth = 0;
    int w = 0, h = 0;
    GLenum format = GL_RGBA16F;
    bool wantDepth = false;

    void create(int width, int height, GLenum fmt, GLenum filter = GL_LINEAR, bool withDepth = false);
    void resize(int width, int height);
    void bind() const;
    void destroy();
};

// Read a whole text file.  Supports one level of `#include "file"` relative to
// the including file's directory.
std::string readFileWithIncludes(const std::string& path, bool* ok = nullptr);

GLuint makeLUT1D(const std::vector<float>& rgb, int n);           // RGB32F, n x 1
GLuint makeLUT1D_R(const std::vector<float>& v, int n);           // R32F,   n x 1
void   updateLUT1D_R(GLuint tex, const std::vector<float>& v, int n);

// Fullscreen triangle. Bind and draw 3 vertices with no attributes.
GLuint fullscreenVAO();

void checkGL(const char* where);

}  // namespace gfx
