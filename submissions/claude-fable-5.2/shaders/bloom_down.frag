#version 410 core
// 13-tap downsample (Jimenez 2014): wide, stable, no fireflies.
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;        // 1 / source size
uniform int  uFirst;        // first pass: soft-clamp extreme values

vec3 tap(vec2 o) {
    vec3 c = texture(uSrc, vUV + o * uTexel).rgb;
    if (uFirst != 0) {
        // Karis-style weight keeps single ultra-bright pixels from flickering.
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c *= 1.0 / (1.0 + l * 0.02);
    }
    return c;
}

void main() {
    vec3 a = tap(vec2(-2, 2)), b = tap(vec2(0, 2)), c = tap(vec2(2, 2));
    vec3 d = tap(vec2(-2, 0)), e = tap(vec2(0, 0)), f = tap(vec2(2, 0));
    vec3 g = tap(vec2(-2, -2)), h = tap(vec2(0, -2)), i = tap(vec2(2, -2));
    vec3 j = tap(vec2(-1, 1)), k = tap(vec2(1, 1)), l = tap(vec2(-1, -1)), m = tap(vec2(1, -1));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    oColor = vec4(col, 1.0);
}
