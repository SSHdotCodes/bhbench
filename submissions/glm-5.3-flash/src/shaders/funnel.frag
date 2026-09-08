#version 410 core
precision highp float;
in vec2 vUV;        // u: angle cells [0,NA], v: radial cells [0,NR]
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCamPos;
uniform float uRMax;

void main(){
    float r = length(vWorld.xz);
    // grid lines in both directions with screen-space AA
    vec2 g = vUV;
    vec2 fw = fwidth(g);
    vec2 dg = abs(fract(g) - 0.5);
    vec2 ln = 1.0 - smoothstep(vec2(0.0), fw * 1.4, dg - (0.5 - fw * 0.8));
    float atten = 1.0 / (1.0 + (fw.x + fw.y) * 2.2);   // fade sub-pixel cells (anti-moire)
    float line = max(ln.x, ln.y) * atten;

    // depth cue
    float fog = exp(-max(r - 2.0, 0.0) * 0.10);
    vec3 fill = vec3(0.016, 0.038, 0.055);
    vec3 lineCol = mix(vec3(0.10, 0.75, 0.70), vec3(0.35, 0.95, 0.90), line);

    vec3 col = fill + lineCol * line * 1.35;

    // ISCO ring marker (r = 3 rs)
    float isco = exp(-pow((r - 3.0) * 6.0, 2.0));
    col += vec3(0.95, 0.45, 0.15) * isco * 0.55;

    // photon sphere ring (r = 1.5 rs)
    float phs = exp(-pow((r - 1.5) * 9.0, 2.0));
    col += vec3(0.95, 0.60, 0.25) * phs * 0.45;

    float alpha = 0.42 + 0.58 * line;
    alpha *= mix(0.35, 1.0, fog);
    fragColor = vec4(col * mix(0.4, 1.0, fog), alpha);
}
