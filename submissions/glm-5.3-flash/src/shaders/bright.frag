#version 410 core
precision highp float;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uThreshold;
void main(){
    vec3 c = texture(uTex, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(uThreshold, uThreshold + 0.6, lum);
    fragColor = vec4(c * w, 1.0);
}
