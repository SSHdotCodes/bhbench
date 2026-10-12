#version 300 es
precision highp float;
precision highp int;
in float vR;
in float vFog;
out vec4 FragColor;
uniform vec3 uColor;
void main(){
  float a = mix(0.95, 0.08, vFog);
  // Brighten throat near horizon
  a += exp(-(vR-1.0)*1.4)*0.5;
  vec3 c = uColor + vec3(0.20,0.06,0.0)*exp(-(vR-1.0)*1.2);
  FragColor = vec4(c, a);
}
