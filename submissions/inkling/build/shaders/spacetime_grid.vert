#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform float uTime;
uniform vec3 uBlackHolePos;
uniform float uSchwarzschildRadius;

out vec3 vPosition;
out vec3 vNormal;

void main() {
    // Apply spacetime curvature: bend coordinates near black hole
    float r = length(aPos - uBlackHolePos);
    float curvatureFactor = exp(-r / (1.5 * uSchwarzschildRadius + 0.3));
    vec3 curvedPos = aPos;
    
    // "Trapdoor" effect: points near BH are pulled downward
    if (r < 4.0 * uSchwarzschildRadius) {
        float depth = (1.0 - r / (4.0 * uSchwarzschildRadius)) * 2.5;
        curvedPos.y -= depth * curvatureFactor;
        // Radial inward pull
        vec3 dirToBH = normalize(uBlackHolePos - aPos);
        curvedPos += dirToBH * curvatureFactor * 0.8;
    }
    
    vPosition = curvedPos;
    vNormal = aNormal;
    gl_Position = projection * view * model * vec4(curvedPos, 1.0);
}
