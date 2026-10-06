#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 view;
uniform mat4 projection;
uniform float time;

out vec2 texCoord;
out vec3 worldPos;

void main() {
    // Apply gravitational deformation to the grid vertices
    float r = length(aPos.xz);
    float SchwarzschildRadius = 2.0; // in grid units, Rs = 2GM/c^2 normalized
    float radialShift = 0.0;
    if (r > 0.5) {
        // Spatial curvature: dr -> dr / sqrt(1 - Rs/r)
        // This makes the grid "stretch" radially toward the black hole
        float factor = 1.0 - SchwarzschildRadius / r;
        if (factor > 0.001) {
            radialShift = -SchwarzschildRadius * log(factor);
        }
    }

    // Displace downward to create visual "funnel" effect
    float depth = -8.0 * SchwarzschildRadius / max(r, 0.3);
    if (r < SchwarzschildRadius) {
        depth = -20.0; // below event horizon
    }

    vec3 pos = aPos;
    pos.y += depth;

    // Slight radial compression to show spatial stretching
    if (r > 0.5 && r < 30.0) {
        float compressFactor = 1.0 + SchwarzschildRadius / max(r, 0.5) * 0.15;
        pos.x *= compressFactor;
        pos.z *= compressFactor;
    }

    worldPos = pos;
    texCoord = aTexCoord;
    gl_Position = projection * view * vec4(pos, 1.0);
}
