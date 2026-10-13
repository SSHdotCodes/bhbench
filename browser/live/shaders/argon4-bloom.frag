#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
layout(location = 0) out vec4 FragColor;

uniform sampler2D uSourceTex;
uniform vec2      uTexelSize;
uniform vec2      uDirection; // (1,0) horizontal, (0,1) vertical, (0,0) bright-pass extract

void main() {
    if (length(uDirection) < 0.5) {
        vec3 c = texture(uSourceTex, vUV).rgb;
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float knee = smoothstep(0.75, 2.10, lum);
        FragColor = vec4(c * knee, 1.0);
        return;
    }

    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(uSourceTex, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * uTexelSize * (float(i) * 1.75);
        result += texture(uSourceTex, vUV + offset).rgb * weights[i];
        result += texture(uSourceTex, vUV - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
