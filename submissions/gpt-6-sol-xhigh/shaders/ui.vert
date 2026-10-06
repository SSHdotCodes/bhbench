#version 410 core
layout(location = 0) in vec2 aPixel;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
uniform vec2 uWindowSize;
void main() {
    gl_Position = vec4(2.0 * aPixel.x / uWindowSize.x - 1.0,
                       1.0 - 2.0 * aPixel.y / uWindowSize.y, 0.0, 1.0);
    vColor = aColor;
}
