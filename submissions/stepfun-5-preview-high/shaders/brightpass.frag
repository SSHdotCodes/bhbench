#version 330 core
// Bright-pass + 2x2 box downsample for the bloom / halo chain.
precision highp float;

out vec4 outColor;
uniform sampler2D uTex;
uniform vec2  uTexel;     // texel size of the *source* texture
uniform float uThreshold; // brightness knee
uniform float uKnee;

void main()
{
    vec2 uv = gl_FragCoord.xy * uTexel * 2.0;   // half-res target over full-res source
    vec3 c = texture(uTex, uv + uTexel * vec2(-0.5, -0.5)).rgb;
    c += texture(uTex, uv + uTexel * vec2( 0.5, -0.5)).rgb;
    c += texture(uTex, uv + uTexel * vec2(-0.5,  0.5)).rgb;
    c += texture(uTex, uv + uTexel * vec2( 0.5,  0.5)).rgb;
    c *= 0.25;

    float l = max(c.r, max(c.g, c.b));
    float w = smoothstep(uThreshold, uThreshold + uKnee, l);
    outColor = vec4(c * w, 1.0);
}
