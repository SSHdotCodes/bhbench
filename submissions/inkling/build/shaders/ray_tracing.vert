#version 330 core
layout (location = 0) in vec2 aPos;
uniform float uTime;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
out vec2 vTexCoord;
void main() {
    vTexCoord = (aPos + 1.0) * 0.5; // Convert from [-1,1] to [0,1]
    gl_Position = projection * view * model * vec4(aPos.x, aPos.y, 0.0, 1.0);
}
