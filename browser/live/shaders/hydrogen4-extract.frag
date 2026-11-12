#version 300 es
precision highp float;
precision highp int;
uniform sampler2D inHDR;
uniform vec2 outputSize;
out vec4 fragColor;
void main(){
 ivec2 size=textureSize(inHDR,0);
 vec2 uv=vec2(gl_FragCoord.x,outputSize.y-gl_FragCoord.y)/outputSize;
 ivec2 src=ivec2(uv*vec2(size));src.y=size.y-1-src.y;
    vec3 c0 = texelFetch(inHDR, src, 0).rgb;
    vec3 c1 = texelFetch(inHDR, clamp(src + ivec2(1, 0), ivec2(0), size-1), 0).rgb;
    vec3 c2 = texelFetch(inHDR, clamp(src + ivec2(0, -1), ivec2(0), size-1), 0).rgb;
    vec3 c3 = texelFetch(inHDR, clamp(src + ivec2(1, -1), ivec2(0), size-1), 0).rgb;
    vec3 col = 0.25 * (c0 + c1 + c2 + c3);

    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float threshold = 1.05;
    float softKnee = 0.45;
    float rq = clamp(lum - threshold + softKnee, 0.0, 2.0 * softKnee);
    float weight = max(rq * rq / (4.0 * softKnee + 1e-5), max(0.0, lum - threshold)) / max(lum, 1e-5);

    fragColor = vec4(col * clamp(weight, 0.0, 3.5), 1.0);
}
