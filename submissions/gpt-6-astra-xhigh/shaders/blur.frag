#version 410 core
in vec2 uv;out vec4 outputColor;uniform sampler2D source;uniform vec2 direction;uniform int extractBright;
vec3 sampleAt(vec2 p){vec3 c=texture(source,p).rgb;return extractBright==1?max(c-vec3(.65),vec3(0)):c;}
void main(){vec3 c=sampleAt(uv)*.227027;c+=(sampleAt(uv+direction*1.384615)+sampleAt(uv-direction*1.384615))*.316216;c+=(sampleAt(uv+direction*3.230769)+sampleAt(uv-direction*3.230769))*.070270;outputColor=vec4(c,1);}
