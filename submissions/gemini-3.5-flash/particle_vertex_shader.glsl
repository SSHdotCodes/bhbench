#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in float aSize;

out vec3 ParticleColor;

uniform mat4 u_view;
uniform mat4 u_proj;

void main() {
    ParticleColor = aColor;
    vec4 clipPos = u_proj * u_view * vec4(aPos, 1.0);
    gl_Position = clipPos;
    
    // Scale point size by distance (perspective scaling)
    gl_PointSize = clamp(aSize / (clipPos.w + 0.001), 2.0, 64.0);
}
