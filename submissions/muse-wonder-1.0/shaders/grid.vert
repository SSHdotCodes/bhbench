#version 330 core
layout(location=0) in vec3 aPos; // xz plane, y=0, radius in [1, 14]
uniform mat4 uMVP;
uniform float uTime;
out float vR;
out float vFog;
void main(){
  float r = length(aPos.xz);
  vR = r;
  // Flamm's paraboloid embedding of Schwarzschild spatial slice (Rs=1):
  //   z(r) = 2*sqrt(Rs*(r-Rs))  for r > Rs. We dip it downward (-y).
  float depth = 2.0 * sqrt(max(r-1.0, 0.0));
  float y = -depth * 0.42;
  // Subtle ripple from infalling matter (visual only, decays near horizon)
  y += sin(r*2.2 - uTime*1.6) * 0.018 * exp(-(r-1.0)*0.25);
  vec3 pos = vec3(aPos.x, y, aPos.z);
  vec4 cs = uMVP * vec4(pos,1.0);
  vFog = clamp(r/14.0, 0.0, 1.0);
  gl_Position = cs;
}
