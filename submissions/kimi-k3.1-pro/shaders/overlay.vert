#version 330 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;

void main() {
    vec2 ndc = vec2(aPosition.x / uScreen.x * 2.0 - 1.0,
                    1.0 - aPosition.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
