#version 300 es
precision highp float;
precision highp int;
// Bright-pass + downsample for bloom.
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;
void main() {
    vec3 c = texture(uTex, vUV).rgb
           + texture(uTex, vUV + uTexel*vec2( 1.0, 1.0)).rgb
           + texture(uTex, vUV + uTexel*vec2(-1.0, 1.0)).rgb
           + texture(uTex, vUV + uTexel*vec2( 1.0,-1.0)).rgb
           + texture(uTex, vUV + uTexel*vec2(-1.0,-1.0)).rgb;
    c *= 0.2;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(0.85, 2.2, l);
    fragColor = vec4(c * w, 1.0);
}
