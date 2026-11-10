#version 410 core
// 3x3 tent upsample, added to the next finer level.
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;        // 1 / source size
uniform float uRadius;

void main() {
    vec2 d = uTexel * uRadius;
    vec3 c = texture(uSrc, vUV).rgb * 4.0;
    c += (texture(uSrc, vUV + vec2(d.x, 0)).rgb + texture(uSrc, vUV - vec2(d.x, 0)).rgb +
          texture(uSrc, vUV + vec2(0, d.y)).rgb + texture(uSrc, vUV - vec2(0, d.y)).rgb) * 2.0;
    c += texture(uSrc, vUV + d).rgb + texture(uSrc, vUV - d).rgb +
         texture(uSrc, vUV + vec2(d.x, -d.y)).rgb + texture(uSrc, vUV + vec2(-d.x, d.y)).rgb;
    oColor = vec4(c / 16.0, 1.0);
}
