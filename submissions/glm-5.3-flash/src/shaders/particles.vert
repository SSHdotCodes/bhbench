#version 410 core
precision highp float;
// Infalling particles spiralling down the Flamm paraboloid.
layout(location = 0) in vec4 aSeed;   // r0, phi0, speed, size
uniform mat4  uMVP;
uniform float uTime;
uniform float uDepthScale;
out float vAlpha;
out float vHot;
void main(){
    float r0 = aSeed.x, phi0 = aSeed.y, speed = aSeed.z, size = aSeed.w;
    float t = fract(speed * uTime * 0.05 + fract(r0 * 12.9898 + phi0));
    float ease = t * t * (3.0 - 2.0 * t);
    float r = mix(r0, 1.03, ease);

    // angular position: faster winding as r shrinks (Keplerian flavor)
    float phi = phi0 + 2.4 * (pow(1.05 / r, 1.5) - 1.0) + uTime * 0.02;
    float depth = uDepthScale * 2.0 * sqrt(max(r - 1.0, 0.0)) - 9.6;

    vec3 pos = vec3(r * cos(phi), -depth - 0.015, r * sin(phi));
    gl_Position = uMVP * vec4(pos, 1.0);
    float dist = length(pos - vec3(0.0));
    gl_PointSize = size * (38.0 / max(dist, 2.0));
    vAlpha = sin(3.14159265 * t) * (1.0 - smoothstep(0.85, 1.0, t));
    vHot = ease;
}
