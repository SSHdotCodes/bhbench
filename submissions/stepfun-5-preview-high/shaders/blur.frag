#version 330 core
// Separable Gaussian-ish blur (9 taps, executed repeatedly to spread the halo).
precision highp float;

out vec4 outColor;
uniform sampler2D uTex;
uniform vec2  uTexel;
uniform vec2  uDir;       // (1,0) or (0,1) scaled by radius

void main()
{
    vec2 uv = gl_FragCoord.xy * uTexel;
    vec3 sum = texture(uTex, uv).rgb * 0.227027;
    sum += (texture(uTex, uv + uDir * 1.3846).rgb + texture(uTex, uv - uDir * 1.3846).rgb) * 0.316216;
    sum += (texture(uTex, uv + uDir * 3.2308).rgb + texture(uTex, uv - uDir * 3.2308).rgb) * 0.070270;
    outColor = vec4(sum, 1.0);
}
