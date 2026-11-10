#version 410 core

// 9-tap tent upsample, added onto the next-larger bloom level.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSrc;      // smaller level
uniform vec2 uTexel;         // 1 / source (small) resolution
uniform float uRadius;

void main() {
    vec2 t = uTexel * uRadius;
    vec3 s = texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb * 1.0;
    s += texture(uSrc, vUV + vec2( 0.0, -t.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2( t.x, -t.y)).rgb * 1.0;
    s += texture(uSrc, vUV + vec2(-t.x,  0.0)).rgb * 2.0;
    s += texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV + vec2( t.x,  0.0)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(-t.x,  t.y)).rgb * 1.0;
    s += texture(uSrc, vUV + vec2( 0.0,  t.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2( t.x,  t.y)).rgb * 1.0;
    s *= (1.0 / 16.0);

    fragColor = vec4(s, 1.0);
}
