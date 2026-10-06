#version 410 core
in vec2 uv;
out vec4 frag;
uniform vec2 resolution;
uniform vec3 eye, forward, right, up;
uniform float tanHalfFov, simTime, stepSize;
uniform int diskEnabled, lensingEnabled, colorMode, turbulence, diagnostic;
const float PI=3.141592653589793;

// Exact Schwarzschild null orbit in its conserved orbital plane, u = M/r.
vec2 acceleration(vec2 s) { return vec2(s.y,-s.x+3.0*s.x*s.x); }
vec2 rk4(vec2 s,float h) {
    vec2 a=acceleration(s), b=acceleration(s+.5*h*a);
    vec2 c=acceleration(s+.5*h*b), d=acceleration(s+h*c);
    return s+h*(a+2.0*b+2.0*c+d)/6.0;
}
float hash(vec3 p) {
    p=fract(p*vec3(.1031,.11369,.13787)); p+=dot(p,p.yzx+19.19);
    return fract((p.x+p.y)*p.z);
}
vec3 sky(vec3 d) {
    // Synthetic source sphere at infinity. Stars are fixed to angular cells.
    vec3 q=d*270.0, cell=floor(q), f=fract(q)-.5;
    float seed=hash(cell);
    float star=exp(-dot(f,f)*180.0)*step(.990,seed)*(.5+5.0*pow(seed,30.0));
    vec3 tint=mix(vec3(.56,.72,1.0),vec3(1.0,.82,.55),hash(cell+31.0));
    float band=exp(-pow((d.y+.25*d.x)/.17,2.0));
    float clouds=.5+.5*sin(17.0*d.z+3.0*sin(12.0*d.x));
    return vec3(.0015,.0025,.005)+vec3(.010,.012,.020)*band*clouds+tint*star;
}
float flux(float r) {
    if(r<=6.0) return 0.0;
    float x=sqrt(r), x0=sqrt(6.0), a=sqrt(3.0);
    float integral=x-x0-.5*a*log((x-a)*(x0+a)/((x+a)*(x0-a)));
    return max(0.0,1.5*integral/(pow(r,2.5)*(r-3.0)));
}
vec3 diskColor(float r,float azimuth,float lookback,float g) {
    float F=flux(r)/.000171;
    float omega=pow(r,-1.5);
    // Emissivity fluctuations are illustrative, passively advected at exact circular Omega.
    float phase=azimuth-omega*(simTime-lookback);
    float textureFactor=1.0;
    if(turbulence!=0) {
        textureFactor=1.0+.12*sin(phase*13.0+3.0*log(r))*sin(phase*7.0-2.4*r)
                         +.08*sin(phase*31.0+1.7*r);
    }
    F*=textureFactor;
    float bolometric=F*pow(g,4.0);
    if(colorMode==1) {
        // Frequency-shift diagnostic: red below unity, blue above unity.
        vec3 c=mix(vec3(1,.16,.06),vec3(.92,.91,.80),smoothstep(.4,1.0,g));
        c=mix(c,vec3(.14,.57,1.0),smoothstep(1.0,1.5,g));
        return c*bolometric*1.8;
    }
    if(colorMode==2) {
        // Three narrow Planck bands at 610/550/460 nm, T_peak=100,000 K.
        // I_nu/nu^3 invariant means a thermal spectrum is B_nu(g*T).
        float T=100000.0*pow(max(F,1e-8),.25)*g;
        vec3 lambda=vec3(610e-9,550e-9,460e-9);
        vec3 exponent=.01438777/(lambda*T);
        vec3 bands=pow(vec3(550e-9)/lambda,vec3(3.0))/(exp(exponent)-1.0);
        return bands*.075;
    }
    // Warm thermal false color. Brightness is bolometric g^4, not visible RGB photometry.
    vec3 color=mix(vec3(.95,.19,.025),vec3(1.0,.64,.20),clamp(pow(F,.25),0.0,1.0));
    color=mix(color,vec3(1.0,.92,.69),smoothstep(.9,1.5,g));
    return color*bolometric*2.4;
}
float hermite(float u0,float v0,float u1,float v1,float h,float t) {
    float t2=t*t,t3=t2*t;
    return (2.0*t3-3.0*t2+1.0)*u0+(t3-2.0*t2+t)*h*v0
         +(-2.0*t3+3.0*t2)*u1+(t3-t2)*h*v1;
}
float timeRate(float u,float b) { return 1.0/(b*max(u*u,1e-12)*max(1.0-2.0*u,1e-5)); }
void main() {
    vec2 pixel=(uv*2.0-1.0)*vec2(resolution.x/resolution.y,1.0)*tanHalfFov;
    vec3 direction=normalize(forward+pixel.x*right+pixel.y*up);
    float R=length(eye), fR=1.0-2.0/R;
    vec3 er=eye/R;
    float nr=dot(direction,er), nt=length(direction-nr*er);
    if(nt<1e-5) { frag=vec4(nr<0.0?vec3(0):sky(direction),1); return; }
    vec3 et=normalize(direction-nr*er), normal=cross(er,et);
    float b=R*nt/sqrt(fR);
    vec2 s=vec2(1.0/R,-nr/b);
    float phi=0.0, lookback=0.0,hitShift=0.0,hitTime=0.0;
    vec3 radiance=vec3(0);
    int status=2;
    if(lensingEnabled==0) {
        float nearT=-dot(eye,direction);
        float closest2=dot(eye,eye)-nearT*nearT;
        bool blocked=nearT>0.0 && closest2<4.0;
        float sphereT=blocked?nearT-sqrt(max(0.0,4.0-closest2)):1e20;
        if(diskEnabled!=0 && abs(direction.y)>.00001) {
            float d=-eye.y/direction.y;
            vec3 hit=eye+d*direction; float r=length(hit);
            if(d>0.0 && d<sphereT && r>=6.0 && r<=24.0) {
                radiance=diskColor(r,atan(-hit.z,hit.x),d,1.0);
                frag=vec4(radiance,1); return;
            }
        }
        frag=vec4(blocked?vec3(0):sky(direction),1); return;
    }
    // RK4 steps in orbital angle. High-order ring structure is subject to finite resolution.
    for(int i=0;i<2400;++i) {
        float h=stepSize;
        vec2 next=rk4(s,h);
        float nextPhi=phi+h;
        float y0=er.y*cos(phi)+et.y*sin(phi);
        float y1=er.y*cos(nextPhi)+et.y*sin(nextPhi);
        // Simpson quadrature on cubic dense output for coordinate light travel time.
        float umid=hermite(s.x,s.y,next.x,next.y,h,.5);
        float dt=h*(timeRate(s.x,b)+4.0*timeRate(umid,b)+timeRate(next.x,b))/6.0;
        if(diskEnabled!=0 && y0*y1<0.0) {
            // Solve the plane crossing angle exactly, then use a cubic dense output for radius.
            float crossing=atan(-er.y,et.y);
            crossing+=ceil((phi-crossing)/PI)*PI;
            float a=clamp((crossing-phi)/h,0.0,1.0);
            float uh=hermite(s.x,s.y,next.x,next.y,h,a);
            float rh=1.0/uh;
            if(uh>0.0 && rh>=6.0 && rh<=24.0) {
                vec3 p=(er*cos(crossing)+et*sin(crossing))*rh;
                // The future-directed emitted photon has Lz/E = -b * normal.y.
                float g=sqrt(1.0-3.0/rh)/(sqrt(fR)*(1.0+pow(rh,-1.5)*b*normal.y));
                float halfU=hermite(s.x,s.y,next.x,next.y,h,a*.5);
                float partialDt=a*h*(timeRate(s.x,b)+4.0*timeRate(halfU,b)+timeRate(uh,b))/6.0;
                hitShift=g; hitTime=lookback+partialDt;
                radiance=diskColor(rh,atan(-p.z,p.x),lookback+partialDt,g);
                status=3; s=vec2(uh,0); phi=crossing; break; // Opaque disk, first emitting intersection.
            }
        }
        if(next.x>=.5) { status=0; s=next; phi=nextPhi; break; }
        if(next.x<=0.0) {
            // Refine u=0 using the cubic dense output. The asymptotic radial direction is the sky direction.
            float lo=0.0, hi=1.0;
            for(int k=0;k<8;++k) {
                float a=(lo+hi)*.5;
                if(hermite(s.x,s.y,next.x,next.y,h,a)>0.0) lo=a; else hi=a;
            }
            phi+=h*(lo+hi)*.5; s=next; status=1;
            radiance=sky(normalize(er*cos(phi)+et*sin(phi))); break;
        }
        lookback+=dt; s=next; phi=nextPhi;
    }
    if(diagnostic!=0) { frag=status==3?vec4(s.x,hitShift,hitTime,3.0):vec4(s.x,s.y,phi,float(status)); return; }
    // Unresolved near-critical rays are marked violet rather than silently called captured.
    if(status==2) radiance=vec3(.16,.0,.22);
    frag=vec4(radiance,1);
}
