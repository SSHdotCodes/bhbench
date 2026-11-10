#version 300 es
precision highp float;
precision highp int;
in vec4 vColor;
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCamPos;
void main() {
    // Fade grid lines that lie behind the black hole's shadow silhouette.
    // The grid is a coordinate-space diagram; the shadow (critical impact
    // parameter b_c = 3*sqrt(3)/2 RS) must stay black.
    vec3 toP = vWorld - uCamPos;
    float dist = length(toP);
    vec3 dir = toP / dist;
    float b = length(cross(vWorld, dir));      // line's impact parameter about origin
    float tClosest = -dot(uCamPos, dir);       // param of closest approach to origin
    float behind = step(tClosest, dist);       // fragment past the hole?
    float shadowFade = smoothstep(2.45, 3.05, b);
    float a = vColor.a * mix(1.0, shadowFade, behind);
    if (a < 0.003) discard;
    fragColor = vec4(vColor.rgb, a);
}
