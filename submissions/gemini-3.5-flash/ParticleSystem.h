#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <vector>

struct Particle {
    float R;
    float phi;
    float fallSpeed;
    float orbitSpeed;
    float size;
    float color[3];
};

class ParticleSystem {
public:
    ParticleSystem(int count, float Rs, float Rmax);
    ~ParticleSystem();

    void update(float dt);
    void draw();

private:
    int count;
    float Rs;
    float Rmax;

    std::vector<Particle> particles;
    unsigned int VAO, VBO;
    std::vector<float> vertexData; // holds x, y, z, r, g, b, size

    void initParticles();
    void updateBuffers();
};

#endif
