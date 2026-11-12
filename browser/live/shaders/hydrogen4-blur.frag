#version 300 es
precision highp float;
precision highp int;
// Separable normalized Gaussian approximation of MPSImageGaussianBlur.
// Native sigmas are 5 and 20 pixels at half resolution; clamp edges, 3-sigma support.
in vec2 vUV;
uniform sampler2D sourceTex;
uniform vec2 direction;
uniform float sigma;
out vec4 fragColor;
void main(){
 vec3 sum=vec3(0.0);float weight=0.0;
 for(int i=-60;i<=60;i++){
  if(abs(float(i))>3.0*sigma)continue;
  float w=exp(-0.5*float(i*i)/(sigma*sigma));
  sum+=texture(sourceTex,vUV+direction*float(i)).rgb*w;weight+=w;
 }
 fragColor=vec4(sum/weight,1.0);
}
