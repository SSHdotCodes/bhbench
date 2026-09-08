#version 410 core
in vec2 uv;out vec4 outputColor;uniform sampler2D source,bloomImage;uniform float exposure,bloomStrength;
void main(){vec3 c=(texture(source,uv).rgb+texture(bloomImage,uv).rgb*bloomStrength)*exposure;c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.,1.);outputColor=vec4(pow(c,vec3(1./2.2)),1);}
