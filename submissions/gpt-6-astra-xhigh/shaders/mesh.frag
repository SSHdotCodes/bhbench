#version 410 core
in vec3 world;out vec4 outputColor;
void main(){
 float r=length(world.xz),a=atan(world.z,world.x);
 vec2 coord=vec2(r,a*24./3.14159265359);
 vec2 width=max(fwidth(coord),vec2(.0001));vec2 dist=abs(fract(coord-.5)-.5)/width;
 float grid=1.-smoothstep(.45,1.1,min(dist.x,dist.y));
 float iso=1.-smoothstep(.02,.10,abs(r-6.));
 float ph=1.-smoothstep(.015,.06,abs(r-3.));
 vec3 base=mix(vec3(.007,.019,.026),vec3(.028,.065,.076),smoothstep(2.,30.,r));
 vec3 color=base+grid*mix(vec3(.16,.45,.53),vec3(.065,.19,.22),smoothstep(2.,30.,r));
 color=mix(color,vec3(.70,.47,.22),iso*.9);color=mix(color,vec3(.28,.55,.66),ph*.95);
 outputColor=vec4(color,1.);
}
