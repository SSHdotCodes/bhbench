#version 410 core
// Displaces a flat polar grid using Flamm's paraboloid, the exact
// embedding of a Schwarzschild equatorial slice into flat 3D space:
//
//     z(r) = 2 * sqrt(rs * (r - rs)),   r >= rs
//
// We render it as -z so the grid sinks into a funnel ("the trapdoor")
// toward the horizon and is flat far away, matching the familiar
// rubber-sheet visualization while remaining an exact GR embedding
// diagram rather than an artistic approximation.

layout(location = 0) in vec2 aPos; // (x, z) on the flat undisplaced grid

uniform mat4 uView;
uniform mat4 uProj;
uniform float uRs;

out float vR;
out float vY;

void main() {
    float r = length(aPos);
    float y = -2.0 * sqrt(uRs * max(r - uRs, 0.0));
    vec3 worldPos = vec3(aPos.x, y, aPos.y);
    vR = r;
    vY = y;
    gl_Position = uProj * uView * vec4(worldPos, 1.0);
}
