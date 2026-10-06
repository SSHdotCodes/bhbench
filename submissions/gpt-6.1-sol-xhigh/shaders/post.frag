#version 410 core
in vec2 uv;
out vec4 frag;
uniform sampler2D scene, bloomTexture;
uniform float exposure, bloomStrength;
void main() {
    vec3 c=texture(scene,uv).rgb+texture(bloomTexture,uv).rgb*bloomStrength;
    c*=exposure;
    // ACES-style display tone mapping, then a display gamma approximation.
    c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.0,1.0);
    frag=vec4(pow(c,vec3(1.0/2.2)),1.0);
}
