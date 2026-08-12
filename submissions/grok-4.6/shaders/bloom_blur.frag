#version 410 core
in vec2 vUv;
uniform sampler2D uImage;
uniform vec2 uDirection;
out vec4 fragColor;
void main() {
    vec2 texel = uDirection / vec2(textureSize(uImage, 0));
    vec3 acc = texture(uImage, vUv).rgb * 0.227027;
    acc += texture(uImage, vUv + texel * 1.384615).rgb * 0.316216;
    acc += texture(uImage, vUv - texel * 1.384615).rgb * 0.316216;
    acc += texture(uImage, vUv + texel * 3.230769).rgb * 0.070270;
    acc += texture(uImage, vUv - texel * 3.230769).rgb * 0.070270;
    fragColor = vec4(acc, 1.0);
}
