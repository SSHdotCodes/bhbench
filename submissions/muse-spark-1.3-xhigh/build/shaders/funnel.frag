#version 410 core
// Spacetime-funnel fragment shader: flat vertex color, slight vertical fade.
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
