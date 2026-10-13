#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aMeta; // x = radius r/M, y = lineType (0=grid, 1=horizon, 2=photon, 3=isco, 4=ergo, 5=geodesic)

uniform mat4 uViewProj;
out vec4 vColor;
out vec2 vMeta;
out vec3 vWorldPos;

void main() {
    vColor = aColor;
    vMeta = aMeta;
    vWorldPos = aPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
