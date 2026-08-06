#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vN;
out vec4 vC;
out vec3 vP;

void main() {
    vN = mat3(uModel) * aNormal;
    vC = aColor;
    vP = (uModel * vec4(aPos, 1.0)).xyz;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
