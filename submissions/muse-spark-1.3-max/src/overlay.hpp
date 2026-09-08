// Tiny immediate-mode text/box overlay (embedded 5x7 bitmap font, no deps).
#pragma once

#include <string>
#include <vector>

#include "shaders.hpp"

struct Rgba {
    float r, g, b, a;
};

class Overlay {
  public:
    bool init();
    void begin(int win_w, int win_h);
    void panel(float x, float y, float w, float h, Rgba c);
    void text(float x, float y, float scale, const std::string& s, Rgba c);
    void end();

    static float line_width(const std::string& s, float scale) {
        return (float)s.size() * 6.0f * scale;
    }
    static float line_height(float scale) { return 7.0f * scale; }

  private:
    Shader prog_;
    unsigned vao_ = 0, vbo_ = 0;
    int u_res_ = -1;
    int win_w_ = 0, win_h_ = 0;
    std::vector<float> verts_;  // x, y, r, g, b, a
    void quad(float x, float y, float w, float h, Rgba c);
};
