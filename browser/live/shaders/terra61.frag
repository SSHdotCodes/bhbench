#version 300 es
precision highp float;
precision highp int;
in vec2 uv;
out vec4 fragColor;
uniform vec2 resolution;
uniform float time, yaw, inclination, zoom;
uniform int mode, diskEnabled;
const float PI=3.141592653589793;
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+1.0),f.x),f.y);}
vec3 sky(vec3 d){
 d=normalize(d);vec2 s=vec2(atan(d.z,d.x)/(2.0*PI)+0.5,asin(clamp(d.y,-1.0,1.0))/PI+0.5);
 float band=exp(-pow((d.y+0.15*sin(atan(d.z,d.x)*2.0))*4.8,2.0));
 vec3 col=vec3(0.004,0.008,0.020)+band*vec3(0.012,0.021,0.039)*(0.35+noise(s*vec2(95,45)));
 for(int layer=0;layer<2;layer++){float scale=layer==0?180.0:420.0;vec2 p=s*vec2(scale*2.0,scale);vec2 cell=floor(p),f=fract(p);float h=hash21(cell+float(layer)*47.0);
  if(h>0.992){vec2 center=vec2(hash21(cell+9.1),hash21(cell+31.7));float dist=length(f-center);float star=exp(-dist*dist*90.0)*pow((h-0.992)/0.008,2.0);col+=star*mix(vec3(0.55,0.72,1.0),vec3(1.0,0.73,0.48),hash21(cell+77.0))*1.3;}}
 return col;
}
// Schwarzschild null geodesics in areal Cartesian coordinates; M=G=c=1.
vec3 acceleration(vec3 p,float l2){float r=length(p);return -3.0*l2*p/pow(r,5.0);}
vec3 diskColor(vec3 p,vec3 backwardVelocity){
 float r=length(p),f=1.0-2.0/r;vec3 er=p/r;vec3 localBack=normalize(backwardVelocity+(1.0/sqrt(f)-1.0)*dot(backwardVelocity,er)*er);
 vec3 photon=-localBack,tangent=normalize(vec3(-p.z,0.0,p.x));float beta=sqrt(1.0/(r-2.0)),gamma=1.0/sqrt(1.0-beta*beta);
 float g=sqrt(f/(1.0-2.0/30.0))/(gamma*(1.0-beta*dot(tangent,photon)));
 float angle=atan(p.z,p.x),phase=angle-time*0.25/pow(r/6.0,1.5);
 float turbulence=0.68+0.32*noise(vec2(phase*13.0,r*2.1)+vec2(sin(phase*3.0)*2.0,0));
 float edge=smoothstep(6.0,7.0,r)*(1.0-smoothstep(18.0,22.0,r));float flux=pow(6.0/r,2.1)*edge*turbulence;
 vec3 hot=mix(vec3(1.0,0.12,0.018),vec3(1.0,0.72,0.30),clamp(pow(6.0/r,0.7)*g,0.0,1.0));
 hot=mix(hot,vec3(1.0,0.96,0.82),clamp((g-1.0)*0.8,0.0,0.5));return hot*flux*pow(g,4.0)*4.2;
}
vec3 lens(vec2 screen){
 vec3 cam=30.0*vec3(sin(yaw)*cos(inclination),sin(inclination),cos(yaw)*cos(inclination));
 vec3 forward=normalize(-cam),right=normalize(cross(forward,vec3(0,1,0))),up=cross(right,forward);
 vec3 ray=normalize(forward+zoom*(screen.x*right+screen.y*up));vec3 er=normalize(cam);float f0=1.0-2.0/30.0;
 vec3 p=cam,v=ray+(sqrt(f0)-1.0)*dot(ray,er)*er;float l2=dot(cross(p,v),cross(p,v));
 for(int i=0;i<480;i++){float r=length(p);if(r<2.015)return vec3(0.0);if(r>48.0&&dot(p,v)>0.0)return sky(v);
  float h=clamp(0.035*r,0.055,0.9);vec3 a=acceleration(p,l2),next=p+v*h+0.5*a*h*h;vec3 nextV=v+0.5*(a+acceleration(next,l2))*h;
  if(diskEnabled==1&&p.y*next.y<0.0){float t=p.y/(p.y-next.y);vec3 hit=mix(p,next,t);float hr=length(hit);if(hr>=6.0&&hr<=22.0)return diskColor(hit,mix(v,nextV,t));}p=next;v=nextV;}
 return vec3(0.0);
}
// Isometric embedding of the equatorial, constant Schwarzschild-time spatial slice.
float surface(float r){return 2.0*sqrt(2.0*max(r-2.0,0.0))-8.0;}
vec3 geometry(vec2 screen){
 vec3 cam=vec3(20.0*sin(yaw+0.5),15.5,20.0*cos(yaw+0.5));vec3 target=vec3(0,-1.1,0),fw=normalize(target-cam),rt=normalize(cross(fw,vec3(0,1,0))),up=cross(rt,fw);
 vec3 rd=normalize(fw+0.65*zoom/1.0*(screen.x*rt+screen.y*up));vec3 bg=vec3(0.008,0.014,0.026)+0.012*max(0.0,1.0-length(screen));float prev=0.0;bool had=false;float old=0.0;
 for(int i=0;i<240;i++){float t=float(i)*0.25;vec3 p=cam+rd*t;float r=length(p.xz);if(r<2.0||r>18.0){had=false;continue;}float val=p.y-surface(r);
  if(had&&val*old<0.0){float a=prev,b=t;for(int j=0;j<7;j++){float m=(a+b)*0.5;vec3 q=cam+rd*m;float v=q.y-surface(length(q.xz));if(v*old>0.0)a=m;else b=m;}
   p=cam+rd*((a+b)*0.5);r=length(p.xz);float ang=atan(p.z,p.x);float radial=abs(fract((r-2.0)*0.5+0.5)-0.5),angular=abs(fract(ang*12.0/(2.0*PI)+0.5)-0.5);
   float lines=max(1.0-smoothstep(0.0,0.055,radial),1.0-smoothstep(0.0,0.035,angular));float horizon=1.0-smoothstep(0.0,0.16,abs(r-2.0)),photon=1.0-smoothstep(0.0,0.11,abs(r-3.0));
   vec3 normal=normalize(vec3(-p.x/r*sqrt(2.0/max(r-2.0,0.01)),1.0,-p.z/r*sqrt(2.0/max(r-2.0,0.01))));float light=0.35+0.65*abs(dot(normal,normalize(vec3(-0.3,0.8,0.5))));
   return vec3(0.015,0.040,0.060)*light+lines*vec3(0.09,0.43,0.48)*light+horizon*vec3(1.0,0.33,0.07)+photon*vec3(0.30,0.28,0.12);
  }old=val;prev=t;had=true;}
 return bg;
}
void main(){vec2 screen=(gl_FragCoord.xy-0.5*resolution)/resolution.y;vec3 col=mode==0?lens(screen):geometry(screen);col=col/(1.0+col);col=pow(max(col,0.0),vec3(1.0/2.2));col*=clamp(1.0-0.30*dot(screen,screen),0.55,1.0);fragColor=vec4(col,1.0);}
