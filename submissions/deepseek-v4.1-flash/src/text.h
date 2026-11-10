// Minimal 5x7 bitmap font renderer. No external dependencies: the glyphs are
// stored as ASCII art and baked into a texture atlas at startup.
#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>

namespace text {

class Renderer {
public:
    void init();
    void shutdown();

    // Begins a batch in screen pixels (origin top-left, y down).
    void begin(int screenW, int screenH);
    void draw(float x, float y, float scale, const std::string& s,
              float r, float g, float b, float a = 1.0f);
    void end();

    static float width(float scale, const std::string& s) {
        return static_cast<float>(s.size()) * 6.0f * scale;
    }
    static float lineHeight(float scale) { return 8.0f * scale; }

private:
    struct Vert {
        float x, y, u, v;
        float r, g, b, a;
    };

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint atlas_ = 0;
    int screenW_ = 1, screenH_ = 1;
    std::vector<Vert> verts_;
};

}  // namespace text
