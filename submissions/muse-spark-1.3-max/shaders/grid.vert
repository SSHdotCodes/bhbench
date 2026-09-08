#version 410 core
// Generic 3D shader for the spacetime-embedding scene.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uCamPos;
uniform float uPointSize;   // >0: point sprites, size/distance attenuation

out vec4 vColor;
out vec3 vNormalW;
out vec3 vPosW;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vPosW = wp.xyz;
    vNormalW = mat3(uModel) * aNormal;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
    if (uPointSize > 0.0) {
        float d = max(length(uCamPos - wp.xyz), 0.5);
        gl_PointSize = clamp(uPointSize / d, 1.5, 22.0);
    }
}
