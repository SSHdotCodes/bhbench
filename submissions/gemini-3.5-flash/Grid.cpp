#include "Grid.h"
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Grid::Grid(float Rs, float Rmax, int numRadial, int numConcentric)
    : Rs(Rs), Rmax(Rmax), numRadial(numRadial), numConcentric(numConcentric),
      gridVAO(0), gridVBO(0), gridVertexCount(0),
      sphereVAO(0), sphereVBO(0), sphereEBO(0), sphereIndexCount(0) {
    generateGrid();
    generateHorizonSphere();
}

Grid::~Grid() {
    if (gridVAO) glDeleteVertexArrays(1, &gridVAO);
    if (gridVBO) glDeleteBuffers(1, &gridVBO);
    if (sphereVAO) glDeleteVertexArrays(1, &sphereVAO);
    if (sphereVBO) glDeleteBuffers(1, &sphereVBO);
    if (sphereEBO) glDeleteBuffers(1, &sphereEBO);
}

void Grid::generateGrid() {
    std::vector<float> vertices;

    float Rmin = Rs * 1.01f; // Just outside horizon to avoid division by zero or negative sqrt

    // 1. Concentric rings
    for (int k = 0; k < numConcentric; ++k) {
        float u = (float)k / (float)(numConcentric - 1);
        // Quadratic distribution to cluster rings near the horizon
        float R = Rmin + (Rmax - Rmin) * u * u;
        float z = 2.0f * std::sqrt(Rs) * (std::sqrt(R - Rs) - std::sqrt(Rmax - Rs));

        for (int j = 0; j < numRadial; ++j) {
            float phi1 = 2.0f * (float)M_PI * (float)j / (float)numRadial;
            float phi2 = 2.0f * (float)M_PI * (float)(j + 1) / (float)numRadial;

            // Segment start
            vertices.push_back(R * std::cos(phi1));
            vertices.push_back(R * std::sin(phi1));
            vertices.push_back(z);

            // Segment end
            vertices.push_back(R * std::cos(phi2));
            vertices.push_back(R * std::sin(phi2));
            vertices.push_back(z);
        }
    }

    // 2. Radial lines
    for (int j = 0; j < numRadial; ++j) {
        float phi = 2.0f * (float)M_PI * (float)j / (float)numRadial;

        for (int k = 0; k < numConcentric - 1; ++k) {
            float u1 = (float)k / (float)(numConcentric - 1);
            float R1 = Rmin + (Rmax - Rmin) * u1 * u1;
            float z1 = 2.0f * std::sqrt(Rs) * (std::sqrt(R1 - Rs) - std::sqrt(Rmax - Rs));

            float u2 = (float)(k + 1) / (float)(numConcentric - 1);
            float R2 = Rmin + (Rmax - Rmin) * u2 * u2;
            float z2 = 2.0f * std::sqrt(Rs) * (std::sqrt(R2 - Rs) - std::sqrt(Rmax - Rs));

            // Segment start
            vertices.push_back(R1 * std::cos(phi));
            vertices.push_back(R1 * std::sin(phi));
            vertices.push_back(z1);

            // Segment end
            vertices.push_back(R2 * std::cos(phi));
            vertices.push_back(R2 * std::sin(phi));
            vertices.push_back(z2);
        }
    }

    gridVertexCount = (int)(vertices.size() / 3);

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);

    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Grid::generateHorizonSphere() {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    const int Y_SEGMENTS = 30;
    const int X_SEGMENTS = 30;
    float radius = Rs;

    for (int y = 0; y <= Y_SEGMENTS; ++y) {
        for (int x = 0; x <= X_SEGMENTS; ++x) {
            float xSegment = (float)x / (float)X_SEGMENTS;
            float ySegment = (float)y / (float)Y_SEGMENTS;
            float xPos = std::cos(xSegment * 2.0f * (float)M_PI) * std::sin(ySegment * (float)M_PI);
            float yPos = std::sin(xSegment * 2.0f * (float)M_PI) * std::sin(ySegment * (float)M_PI);
            float zPos = std::cos(ySegment * (float)M_PI);

            // Position
            vertices.push_back(radius * xPos);
            vertices.push_back(radius * yPos);
            vertices.push_back(radius * zPos);
            
            // Normal (same as position for a sphere of radius Rs)
            vertices.push_back(xPos);
            vertices.push_back(yPos);
            vertices.push_back(zPos);
        }
    }

    for (int y = 0; y < Y_SEGMENTS; ++y) {
        for (int x = 0; x < X_SEGMENTS; ++x) {
            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
            indices.push_back(y * (X_SEGMENTS + 1) + x);
            indices.push_back(y * (X_SEGMENTS + 1) + x + 1);

            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
            indices.push_back(y * (X_SEGMENTS + 1) + x + 1);
            indices.push_back((y + 1) * (X_SEGMENTS + 1) + x + 1);
        }
    }

    sphereIndexCount = (int)indices.size();

    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);

    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Grid::drawGrid() {
    glBindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, gridVertexCount);
    glBindVertexArray(0);
}

void Grid::drawHorizon() {
    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
