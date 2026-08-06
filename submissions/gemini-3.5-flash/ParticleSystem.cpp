#include "ParticleSystem.h"
#include <cmath>
#include <cstdlib>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper for random floats
static float randFloat(float min, float max) {
    return min + static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) / (max - min));
}

ParticleSystem::ParticleSystem(int count, float Rs, float Rmax)
    : count(count), Rs(Rs), Rmax(Rmax), VAO(0), VBO(0) {
    initParticles();
    
    // Allocate buffer data: each vertex has 7 floats: x, y, z, r, g, b, size
    vertexData.resize(count * 7);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Size attribute
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

ParticleSystem::~ParticleSystem() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
}

void ParticleSystem::initParticles() {
    particles.resize(count);
    for (int i = 0; i < count; ++i) {
        particles[i].R = randFloat(Rs * 1.1f, Rmax);
        particles[i].phi = randFloat(0.0f, 2.0f * (float)M_PI);
        
        // Relativistic spiraling parameters
        particles[i].fallSpeed = randFloat(0.15f, 0.4f);
        particles[i].orbitSpeed = randFloat(2.5f, 4.5f);
        particles[i].size = randFloat(8.0f, 24.0f);
        
        // Glowing orange/yellow/white plasma colors
        float choice = randFloat(0.0f, 1.0f);
        if (choice < 0.6f) { // Orange
            particles[i].color[0] = 1.0f;
            particles[i].color[1] = randFloat(0.3f, 0.6f);
            particles[i].color[2] = 0.0f;
        } else if (choice < 0.9f) { // Yellow-White
            particles[i].color[0] = 1.0f;
            particles[i].color[1] = randFloat(0.8f, 1.0f);
            particles[i].color[2] = randFloat(0.5f, 0.8f);
        } else { // Soft Blue (hot gas)
            particles[i].color[0] = 0.5f;
            particles[i].color[1] = 0.8f;
            particles[i].color[2] = 1.0f;
        }
    }
}

void ParticleSystem::update(float dt) {
    for (int i = 0; i < count; ++i) {
        Particle& p = particles[i];
        
        // R decreases (falls in), accelerating as it gets closer: fall_rate = fallSpeed / R^2
        float rSq = p.R * p.R;
        p.R -= (p.fallSpeed / rSq) * dt * 2.0f;
        
        // phi increases (orbits), accelerating Keplerian-style: omega = orbitSpeed / R^1.5
        p.phi += (p.orbitSpeed / (p.R * std::sqrt(p.R))) * dt;
        if (p.phi > 2.0f * (float)M_PI) {
            p.phi -= 2.0f * (float)M_PI;
        }
        
        // Reset if it crosses the event horizon
        if (p.R <= Rs * 1.01f) {
            p.R = randFloat(Rmax * 0.8f, Rmax);
            p.phi = randFloat(0.0f, 2.0f * (float)M_PI);
            p.fallSpeed = randFloat(0.15f, 0.4f);
            p.orbitSpeed = randFloat(2.5f, 4.5f);
            p.size = randFloat(8.0f, 24.0f);
            
            float choice = randFloat(0.0f, 1.0f);
            if (choice < 0.6f) {
                p.color[0] = 1.0f; p.color[1] = randFloat(0.3f, 0.6f); p.color[2] = 0.0f;
            } else if (choice < 0.9f) {
                p.color[0] = 1.0f; p.color[1] = randFloat(0.8f, 1.0f); p.color[2] = randFloat(0.5f, 0.8f);
            } else {
                p.color[0] = 0.5f; p.color[1] = 0.8f; p.color[2] = 1.0f;
            }
        }
        
        // Compute 3D coordinate on Flamm's Paraboloid
        float x = p.R * std::cos(p.phi);
        float y = p.R * std::sin(p.phi);
        float z = 2.0f * std::sqrt(Rs) * (std::sqrt(p.R - Rs) - std::sqrt(Rmax - Rs));
        
        // Write to buffer
        int idx = i * 7;
        vertexData[idx + 0] = x;
        vertexData[idx + 1] = y;
        vertexData[idx + 2] = z;
        vertexData[idx + 3] = p.color[0];
        vertexData[idx + 4] = p.color[1];
        vertexData[idx + 5] = p.color[2];
        vertexData[idx + 6] = p.size;
    }
    
    updateBuffers();
}

void ParticleSystem::updateBuffers() {
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexData.size() * sizeof(float), vertexData.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ParticleSystem::draw() {
    glBindVertexArray(VAO);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);
}
