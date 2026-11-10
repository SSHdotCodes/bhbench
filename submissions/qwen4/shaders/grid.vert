#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uPV;
uniform vec3 uCamPos;
out vec4 vColor;
out vec3 vWorld;
void main() {
    gl_Position = uPV * vec4(aPos, 1.0);
    float dist = length(aPos - uCamPos);
    float fog = exp(-dist * 0.016);
    vColor = vec4(aColor.rgb, aColor.a * fog);
    vWorld = aPos;
}
