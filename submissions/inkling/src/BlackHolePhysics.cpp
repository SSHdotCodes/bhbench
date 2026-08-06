#include "BlackHolePhysics.h"
#include <cmath>
#include <algorithm>

namespace BlackHolePhysics {

PhotonPath tracePhoton(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& bhCenter) {
    PhotonPath path;
    glm::vec3 pos = origin;
    glm::vec3 dir = glm::normalize(direction);
    
    const float Rs = SCHWARZSCHILD_RADIUS;
    const float stepSize = 0.05f;
    const int maxSteps = 500;
    
    for (int i = 0; i < maxSteps; ++i) {
        path.points.push_back(pos);
        
        float r = glm::length(pos - bhCenter);
        if (r < Rs * 1.01f) {
            // Reached near event horizon
            break;
        }
        
        // Deflection: compute impact parameter relative to BH
        float b = glm::length(glm::cross(dir, bhCenter - pos));
        if (b > 0.001f) {
            float alpha = deflectionAngle(b);
            // Apply deflection: rotate direction
            glm::vec3 toBH = glm::normalize(bhCenter - pos);
            glm::vec3 perp = glm::normalize(glm::cross(dir, toBH));
            if (glm::length(perp) < 0.001f) {
                perp = glm::normalize(glm::cross(toBH, glm::vec3(0,1,0)));
            }
            dir = glm::normalize(dir * std::cos(alpha) + perp * std::sin(alpha));
        }
        
        pos += dir * stepSize;
    }
    
    return path;
}

} // namespace BlackHolePhysics
