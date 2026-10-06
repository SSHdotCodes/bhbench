#version 410 core
// Separable 9-tap Gaussian blur (linear-sampling weights).
in vec2 vUV;
layout(location = 0) out vec4 outColor;

uniform sampler2D uTex;
uniform vec2 uDir;

void main() {
    vec3 s = texture(uTex, vUV).rgb * 0.2270270270;
    vec2 o1 = uDir * 1.3846153846;
    vec2 o2 = uDir * 3.2307692308;
    s += (texture(uTex, vUV + o1).rgb + texture(uTex, vUV - o1).rgb) * 0.3162162162;
    s += (texture(uTex, vUV + o2).rgb + texture(uTex, vUV - o2).rgb) * 0.0702702703;
    outColor = vec4(s, 1.0);
}
