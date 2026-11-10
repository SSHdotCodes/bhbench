#version 410 core

// 13-tap downsample with a soft-knee threshold (first level only), following
// the standard "next generation post processing" bloom pyramid.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSrc;
uniform vec2 uTexel;      // 1 / source resolution
uniform int uPrefilter;   // 1 on the first level
uniform float uThreshold;
uniform float uKnee;

vec3 fetch(vec2 uv) { return texture(uSrc, uv).rgb; }

void main() {
    vec2 t = uTexel;
    vec3 a = fetch(vUV + t * vec2(-2, -2));
    vec3 b = fetch(vUV + t * vec2( 0, -2));
    vec3 c = fetch(vUV + t * vec2( 2, -2));
    vec3 d = fetch(vUV + t * vec2(-1, -1));
    vec3 e = fetch(vUV + t * vec2( 1, -1));
    vec3 f = fetch(vUV + t * vec2(-2,  0));
    vec3 g = fetch(vUV);
    vec3 h = fetch(vUV + t * vec2( 2,  0));
    vec3 i = fetch(vUV + t * vec2(-1,  1));
    vec3 j = fetch(vUV + t * vec2( 1,  1));
    vec3 k = fetch(vUV + t * vec2(-2,  2));
    vec3 l = fetch(vUV + t * vec2( 0,  2));
    vec3 m = fetch(vUV + t * vec2( 2,  2));

    vec3 col = (d + e + i + j) * 0.125;
    col += (a + c + k + m) * 0.03125;
    col += (b + f + h + l) * 0.0625;
    col += g * 0.125;

    if (uPrefilter == 1) {
        float br = max(col.r, max(col.g, col.b));
        float soft = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);
        soft = soft * soft / (4.0 * uKnee + 1e-5);
        float contrib = max(soft, br - uThreshold) / max(br, 1e-5);
        col *= contrib;
    }
    fragColor = vec4(col, 1.0);
}
