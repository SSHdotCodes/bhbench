#version 410 core
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNrm;
out vec3 vWorld;
void main(){
    vNrm = mat3(uModel) * aNrm;
    vWorld = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
