#version 410 core
precision highp float;
in float vAlpha;
in float vHot;
out vec4 fragColor;
void main(){
    vec2 d = gl_PointCoord - 0.5;
    float m = exp(-dot(d, d) * 14.0);
    vec3 c = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.92, 0.80), vHot);
    fragColor = vec4(c * m * vAlpha * 1.6, 1.0);
}
