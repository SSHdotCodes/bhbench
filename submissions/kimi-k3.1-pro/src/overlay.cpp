#include "overlay.hpp"

#include "gl_utils.hpp"

#include <array>
#include <cctype>
#include <cstddef>

namespace {
using Glyph = std::array<unsigned char, 7>;

Glyph glyph(char character) {
    switch (character) {
        case 'A': return {14,17,17,31,17,17,17};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {14,4,4,4,4,4,14};
        case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17};
        case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case '-': return {0,0,0,31,0,0,0};
        case '_': return {0,0,0,0,0,0,31};
        case '+': return {0,4,4,31,4,4,0};
        case '=': return {0,0,31,0,31,0,0};
        case '.': return {0,0,0,0,0,12,12};
        case ',': return {0,0,0,0,4,4,8};
        case ':': return {0,12,12,0,12,12,0};
        case ';': return {0,12,12,0,4,4,8};
        case '/': return {1,2,2,4,8,8,16};
        case '\\': return {16,8,8,4,2,2,1};
        case '(': return {2,4,8,8,8,4,2};
        case ')': return {8,4,2,2,2,4,8};
        case '[': return {14,8,8,8,8,8,14};
        case ']': return {14,2,2,2,2,2,14};
        case '<': return {2,4,8,16,8,4,2};
        case '>': return {8,4,2,1,2,4,8};
        case '%': return {17,2,4,8,17,0,0};
        case '!': return {4,4,4,4,4,0,4};
        case '?': return {14,17,1,2,4,0,4};
        case '*': return {0,21,14,31,14,21,0};
        case '^': return {4,10,17,0,0,0,0};
        case '|': return {4,4,4,4,4,4,4};
        case '#': return {10,31,10,10,31,10,0};
        default: return {0,0,0,0,0,0,0};
    }
}
} // namespace

Overlay::Overlay(const std::string& shaderDirectory) {
    program_ = loadProgram(shaderDirectory, "overlay.vert", "overlay.frag");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);
}

Overlay::~Overlay() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
}

void Overlay::begin(float width, float height) {
    width_ = width;
    height_ = height;
    vertices_.clear();
    vertices_.reserve(32768);
}

void Overlay::quad(float x, float y, float width, float height,
                   const Vec3& color, float alpha) {
    const Vertex a{x, y, color.x, color.y, color.z, alpha};
    const Vertex b{x + width, y, color.x, color.y, color.z, alpha};
    const Vertex c{x, y + height, color.x, color.y, color.z, alpha};
    const Vertex d{x + width, y + height, color.x, color.y, color.z, alpha};
    vertices_.insert(vertices_.end(), {a, b, c, b, d, c});
}

void Overlay::rectangle(float x, float y, float width, float height,
                        const Vec3& color, float alpha) {
    quad(x, y, width, height, color, alpha);
}

void Overlay::text(float x, float y, const std::string& value, float scale,
                   const Vec3& color, float alpha) {
    float cursor = x;
    for (unsigned char raw : value) {
        const char character = static_cast<char>(std::toupper(raw));
        const Glyph rows = glyph(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1u << (4 - column))) != 0) {
                    quad(cursor + column * scale, y + row * scale,
                         scale, scale, color, alpha);
                }
            }
        }
        cursor += 6.0f * scale;
    }
}

float Overlay::textWidth(const std::string& text, float scale) const {
    return text.empty() ? 0.0f : (6.0f * text.size() - 1.0f) * scale;
}

void Overlay::draw() {
    if (vertices_.empty()) return;
    glUseProgram(program_);
    glUniform2f(uniform(program_, "uScreen"), width_, height_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex),
                 vertices_.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
