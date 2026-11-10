#version 300 es
precision highp float;
precision highp int;
in vec3 vWorld;
in vec3 vNormal;
in float vR;
in float vFade;
out vec4 FragColor;
uniform float uRs;
uniform int   uMode;
uniform vec3  uColor;
uniform float uTime;
void main(){
  if(uMode == 1){ FragColor = vec4(uColor, 0.95); return; }
  if(uMode == 2){
    vec2 c = gl_PointCoord - 0.5;
    float d = length(c);
    if(d > 0.5) discard;
    float a = smoothstep(0.5, 0.05, d) * vFade;
    FragColor = vec4(vec3(0.8,0.92,1.0)*2.2, a);
    return;
  }
  if(vR < uRs*1.001) discard; // inside horizon: black hole interior
  // Antialiased grid lines on the embedded sheet
  vec2 g = abs(fract(vWorld.xz/2.0) - 0.5);
  vec2 fw = fwidth(vWorld.xz);
  float line = 1.0 - min(min(g.x/max(fw.x,1e-4), g.y/max(fw.y,1e-4)), 1.0);
  line = clamp(line, 0.0, 1.0);
  vec3 L = normalize(vec3(0.5, 0.7, 0.8));
  float diff = max(dot(normalize(vNormal), L), 0.0);
  float curvature = clamp(uRs/max(vR, uRs*1.01), 0.0, 1.0); // 1 at horizon -> 0 far
  vec3 base = mix(vec3(0.02,0.05,0.10), vec3(0.16,0.05,0.02), curvature);
  vec3 lineCol = mix(vec3(0.15,0.55,0.85), vec3(1.0,0.45,0.15), curvature);
  vec3 col = base*(0.35+0.75*diff) + lineCol*line*(0.7+0.6*diff);
  // Landmark rings on the sheet: horizon (red), photon sphere (gold), ISCO (cyan)
  col += vec3(1.0,0.25,0.05) * exp(-abs(vR-uRs)/(0.10*uRs)) * 0.8;
  col += vec3(1.0,0.75,0.20) * exp(-abs(vR-1.5*uRs)/(0.10*uRs)) * 0.55;
  col += vec3(0.20,0.80,1.00) * exp(-abs(vR-3.0*uRs)/(0.12*uRs)) * 0.50;
  // Horizon glow and ripple rings moving inward (visual aid)
  col += vec3(1.0,0.25,0.05) * exp(-(vR-uRs)/(0.18*uRs)) * 0.9;
  float ripple = sin(vR*2.2 - uTime*1.5);
  col += vec3(0.1,0.2,0.3) * ripple * exp(-(vR-uRs)/(3.0*uRs)) * 0.35;
  FragColor = vec4(col, 1.0);
}
