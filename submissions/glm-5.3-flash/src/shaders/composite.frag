#version 410 core
precision highp float;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2  uRes;

vec3 aces(vec3 x){
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
float hash12(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
void main(){
    vec3 c = texture(uScene, vUV).rgb;
    vec3 b = texture(uBloom, vUV).rgb;
    c += b * uBloomStrength;

    // subtle vignette (camera optics, not physics)
    vec2 q = vUV - 0.5;
    c *= 1.0 - 0.22 * dot(q, q) * 2.4;

    c = aces(c * uExposure);
    c = pow(c, vec3(1.0 / 2.2));
    c += (hash12(vUV * uRes + fract(float(uRes.y) * 0.7137)) - 0.5) / 255.0;
    fragColor = vec4(c, 1.0);
}
