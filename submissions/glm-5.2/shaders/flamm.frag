#version 330 core
out vec4 FragColor;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vGrid;            // (r, theta)
uniform float uRs;
uniform vec3  uCamPos;
uniform float uTime;

#define PI 3.14159265358979

float gridLine(float v, float spacing, float width){
    float d = abs(fract(v/spacing - 0.5) - 0.5) * spacing;
    return 1.0 - smoothstep(0.0, width, d);
}

void main(){
    float r = vGrid.x;
    float th = vGrid.y;

    // base surface shading (dark, so the grid pops)
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.4, 0.9, 0.3));
    float diff = clamp(dot(N, L), 0.0, 1.0);
    vec3 base = mix(vec3(0.015,0.02,0.04), vec3(0.06,0.09,0.14), diff);

    // radial + angular grid lines -> the warped lattice on the funnel
    float gr = gridLine(r,  1.0, 0.035);          // every 1 Rs
    float ga = gridLine(th, PI/12.0, 0.04);       // every 15 degrees
    vec3 gridCol = vec3(0.2, 0.6, 1.0);
    base += gridCol * max(gr, ga) * 0.9;

    // throat glow near the event horizon
    float throat = smoothstep(uRs*1.6, uRs, r);
    base += vec3(1.0,0.5,0.15) * throat * (0.6 + 0.4*sin(uTime*2.0));

    // horizon rim
    float rim = smoothstep(uRs*1.05, uRs, r);
    base = mix(base, vec3(0.0), rim);

    // simple fog with distance
    float d = length(vWorldPos - uCamPos);
    float fog = exp(-d*0.04);
    base *= fog;

    FragColor = vec4(base, 1.0);
}
