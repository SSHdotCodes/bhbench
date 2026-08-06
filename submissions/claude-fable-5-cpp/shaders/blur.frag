#version 410 core
// Separable 9-tap Gaussian, ping-ponged horizontally/vertically for bloom halos.
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uDir;   // (texel,0) or (0,texel)

void main() {
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    fragColor = vec4(c, 1.0);
}
