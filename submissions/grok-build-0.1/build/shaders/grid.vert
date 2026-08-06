#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uViewProj;
uniform float uTime;
uniform float uWarp;
uniform float uRs;
uniform vec3 uCamPos;

out float vDist;
out vec3 vWorldPos;

vec3 warpPosition(vec3 p) {
    float r = length(p.xy) + 0.0001;
    if (r < uRs * 0.6) return p; // avoid singularity
    float zoff = 2.0 * sqrt(uRs * max(r - uRs, 0.0));
    // Add some inflow animation to show trapdoor / accretion of space
    float inflow = 0.15 * (uTime * 0.8) / (r + 1.5);
    vec2 xy = p.xy * (1.0 - inflow * 0.6);
    float rr = length(xy) + 0.0001;
    float zz = zoff * uWarp;
    // Slight radial pull visual
    return vec3(xy, p.z - zz);
}

void main() {
    vec3 wp = warpPosition(aPos);
    vWorldPos = wp;
    vDist = length(wp - uCamPos);
    gl_Position = uViewProj * vec4(wp, 1.0);
}
