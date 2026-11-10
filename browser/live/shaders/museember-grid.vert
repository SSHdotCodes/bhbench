#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 aPos;   // grid: (x, y, 0); particle: (seed, angle, 0)
uniform mat4 uMVP;
uniform float uRs;
uniform float uTime;
uniform int   uMode;               // 0 grid mesh, 1 ring, 2 particles
out vec3 vWorld;
out vec3 vNormal;
out float vR;
out float vFade;
void main(){
  vec3 p = aPos;
  vFade = 1.0;
  if(uMode == 2){
    float seed = aPos.x;
    float ang0 = aPos.y;
    float phase = fract(uTime*0.045 + seed);         // 0 at top, 1 at horizon
    float r = mix(11.0*uRs, 1.06*uRs, pow(phase, 0.65));
    float ang = ang0 + phase*7.0 + (1.0-phase)*(1.0-phase)*2.0; // inspiral
    p = vec3(cos(ang)*r, 0.0, sin(ang)*r);
    vFade = smoothstep(0.0,0.12,phase) * (1.0 - smoothstep(0.75,1.0,phase));
  }
  float r = length(p.xz);
  vR = r;
  // Flamm embedding of the equatorial (XZ) slice: y(r) = -sqrt(rs*(r-rs))
  // analytic derivative dy/dr = -0.5*sqrt(rs/(r-rs))
  float rr = max(r - uRs, 0.0);
  float y = -sqrt(uRs*rr);
  float dydR = (rr > 1e-4) ? -0.5*sqrt(uRs/rr) : -8.0;
  vec2 grad = (r > 1e-5) ? (p.xz/r) : vec2(0.0);
  vNormal = normalize(vec3(-dydR*grad.x, 1.0, -dydR*grad.y));
  p.y = y + (uMode == 2 ? 0.35 : 0.0);
  vWorld = p;
  vec4 clip = uMVP * vec4(p, 1.0);
  gl_Position = clip;
  if(uMode == 2){
    float dist = max(clip.w, 0.1);
    gl_PointSize = clamp(260.0/dist, 3.0, 14.0) * (0.6+0.8*fract(aPos.x*7.31));
  }
}
