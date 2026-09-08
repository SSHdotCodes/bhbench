#version 410 core
precision highp float;
in vec3 vNrm;
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCamPos;
void main(){
    // event horizon: black, with the faintest rim so the throat reads in 3D
    vec3 n = normalize(vNrm);
    vec3 v = normalize(uCamPos - vWorld);
    float rim = pow(1.0 - abs(dot(n, v)), 3.0);
    vec3 col = vec3(0.004, 0.005, 0.007) + vec3(0.10, 0.55, 0.55) * rim * 0.35;
    fragColor = vec4(col, 1.0);
}
