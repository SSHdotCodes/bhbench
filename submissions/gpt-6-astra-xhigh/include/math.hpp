#pragma once
#include <cmath>
struct V3 { float x=0,y=0,z=0; };
inline V3 operator+(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline V3 norm(V3 a){return a*(1.f/std::sqrt(dot(a,a)));}
struct Mat4 { float m[16]{}; };
inline Mat4 operator*(const Mat4&a,const Mat4&b){Mat4 c;for(int col=0;col<4;col++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)c.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];return c;}
inline Mat4 perspective(float fov,float aspect,float near,float far){Mat4 m;float f=1/std::tan(fov*.5f);m.m[0]=f/aspect;m.m[5]=f;m.m[10]=(far+near)/(near-far);m.m[11]=-1;m.m[14]=2*far*near/(near-far);return m;}
inline Mat4 lookAt(V3 eye,V3 center){V3 f=norm(center-eye),s=norm(cross(f,{0,1,0})),u=cross(s,f);Mat4 m;m.m[0]=s.x;m.m[4]=s.y;m.m[8]=s.z;m.m[1]=u.x;m.m[5]=u.y;m.m[9]=u.z;m.m[2]=-f.x;m.m[6]=-f.y;m.m[10]=-f.z;m.m[12]=-dot(s,eye);m.m[13]=-dot(u,eye);m.m[14]=dot(f,eye);m.m[15]=1;return m;}
