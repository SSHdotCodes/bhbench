#version 410 core
// Generic line / point program (grid lines, rings, trails, geodesics).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in float aSize;

uniform mat4 uMVP;
uniform float uPointSizeMul;

out vec4 vCol;

void main() {
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = max(aSize * uPointSizeMul, 1.0);
}
