#version 300 es
precision highp float;
precision highp int;
// Separable 9-tap Gaussian.
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uDir;   // texel-sized direction
void main() {
    vec3 c = texture(uTex, vUV).rgb * 0.227027;
    c += (texture(uTex, vUV + uDir*1.3846154).rgb
        + texture(uTex, vUV - uDir*1.3846154).rgb) * 0.3162162;
    c += (texture(uTex, vUV + uDir*3.2307692).rgb
        + texture(uTex, vUV - uDir*3.2307692).rgb) * 0.0702703;
    fragColor = vec4(c, 1.0);
}
