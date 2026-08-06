#ifndef GRID_H
#define GRID_H

#include <vector>

class Grid {
public:
    Grid(float Rs, float Rmax, int numRadial, int numConcentric);
    ~Grid();

    void drawGrid();
    void drawHorizon();

private:
    float Rs;
    float Rmax;
    int numRadial;
    int numConcentric;

    unsigned int gridVAO, gridVBO;
    int gridVertexCount;

    unsigned int sphereVAO, sphereVBO, sphereEBO;
    int sphereIndexCount;

    void generateGrid();
    void generateHorizonSphere();
};

#endif
