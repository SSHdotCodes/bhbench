#version 410 core
// Embedding diagram of the equatorial plane (see physics.hpp, kerr::embedding).
layout(location = 0) in vec3 aPos;      // Euclidean embedding position
layout(location = 1) in vec4 aParam;    // r, phi, rain proper time tau, proper distance ell
layout(location = 2) in float aLapse;   // clock rate relative to infinity

uniform mat4 uViewProj;

out vec3  vWorld;
out vec4  vParam;
out float vLapse;

void main() {
    vWorld = aPos;
    vParam = aParam;
    vLapse = aLapse;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
