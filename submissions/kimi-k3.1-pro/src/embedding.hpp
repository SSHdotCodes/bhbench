#pragma once

#include "math.hpp"

#include <OpenGL/gl3.h>

#include <string>
#include <vector>

class EmbeddingDiagram {
public:
    EmbeddingDiagram(const std::string& shaderDirectory);
    ~EmbeddingDiagram();

    EmbeddingDiagram(const EmbeddingDiagram&) = delete;
    EmbeddingDiagram& operator=(const EmbeddingDiagram&) = delete;

    void draw(const Mat4& mvp) const;

    static constexpr float outerRadius = 30.0f;

    struct Vertex {
        Vec3 position;
        Vec3 color;
    };

private:
    GLuint program_ = 0;
    GLuint surfaceVAO_ = 0;
    GLuint surfaceVBO_ = 0;
    GLuint surfaceEBO_ = 0;
    GLuint linesVAO_ = 0;
    GLuint linesVBO_ = 0;
    GLsizei surfaceIndexCount_ = 0;
    GLsizei lineVertexCount_ = 0;
};
