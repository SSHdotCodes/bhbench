#pragma once

#include "math.hpp"

#include <OpenGL/gl3.h>

#include <string>
#include <vector>

class Overlay {
public:
    explicit Overlay(const std::string& shaderDirectory);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void begin(float width, float height);
    void rectangle(float x, float y, float width, float height,
                   const Vec3& color, float alpha = 1.0f);
    void text(float x, float y, const std::string& text, float scale,
              const Vec3& color, float alpha = 1.0f);
    float textWidth(const std::string& text, float scale) const;
    void draw();

private:
    struct Vertex {
        float x, y;
        float r, g, b, a;
    };

    void quad(float x, float y, float width, float height,
              const Vec3& color, float alpha);

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    float width_ = 1.0f;
    float height_ = 1.0f;
    std::vector<Vertex> vertices_;
};
