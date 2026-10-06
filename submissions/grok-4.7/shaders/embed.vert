#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aRadius;
layout(location = 3) in float aPhi;
layout(location = 4) in float aKind;

uniform mat4 uMVP;
uniform float uPointSize;

out vec3 vNormal;
out vec3 vWorld;
out float vRadius;
out float vPhi;
out float vKind;

void main() {
    vNormal = aNormal;
    vWorld = aPos;
    vRadius = aRadius;
    vPhi = aPhi;
    vKind = aKind;
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize * clamp(10.0 / max(gl_Position.w, 0.2), 0.45, 2.4);
}
