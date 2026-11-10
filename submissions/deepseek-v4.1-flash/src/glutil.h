// Thin OpenGL helpers: shader compilation, buffers, framebuffers.
#pragma once
#include <string>
#include <cstdint>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace glu {

// Compiles and links a program. Throws std::runtime_error with the driver log
// on failure (including the offending source line numbers).
GLuint makeProgram(const char* vertexSrc, const char* fragmentSrc,
                   const char* debugName = "program");

// Fullscreen triangle/quad VAO with a vec2 attribute at location 0.
GLuint makeFullscreenVAO();

struct Framebuffer {
    GLuint fbo = 0;
    GLuint color = 0;   // RGBA16F
    GLuint depth = 0;   // DEPTH_COMPONENT24 (optional)
    int width = 0;
    int height = 0;
    bool hasDepth = false;

    void create(int w, int h, bool withDepth, bool hdr = true);
    void destroy();
    void bind() const;
};

void checkGLError(const char* where);

}  // namespace glu
