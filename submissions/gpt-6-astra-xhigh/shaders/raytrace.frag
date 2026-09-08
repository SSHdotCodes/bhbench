#version 410 core
in vec2 uv;
out vec4 outputColor;
uniform vec2 resolution;
uniform vec3 eye,forward,right,up;
uniform float simTime,stepSize;
uniform int showDisk,showHalo,showStars,showSkyGrid,useGR,useRedshift,useTexture,colorMode,validationMode;
const float PI=3.141592653589793;

float hash13(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float valueNoise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash13(i),hash13(i+vec3(1,0,0)),f.x),mix(hash13(i+vec3(0,1,0)),hash13(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash13(i+vec3(0,0,1)),hash13(i+vec3(1,0,1)),f.x),mix(hash13(i+vec3(0,1,1)),hash13(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){return .53*valueNoise(p)+.27*valueNoise(p*2.03)+.13*valueNoise(p*4.11)+.07*valueNoise(p*8.21);}

// u=1/r; q=du/dphi; third component is positive coordinate lookback time.
// This is the exact Schwarzschild null-orbit ODE, not a Newtonian force law.
vec3 deriv(vec3 s,float b){float f=useGR==1?max(1.-2.*s.x,1.e-5):1.;return vec3(s.y,-s.x+(useGR==1?3.*s.x*s.x:0.),1./(b*max(s.x*s.x,1.e-12)*f));}
vec3 rk4(vec3 s,float h,float b){vec3 a=deriv(s,b),c=deriv(s+a*h*.5,b),d=deriv(s+c*h*.5,b),e=deriv(s+d*h,b);return s+h*(a+2.*c+2.*d+e)/6.;}

float flux(float r){
    float x=sqrt(r),a=sqrt(3.),x0=sqrt(6.);
    float integral=x-x0-.5*a*log(((x-a)/(x+a))/((x0-a)/(x0+a)));
    // Exact zero-torque Page-Thorne profile; normalized by its numerical maximum.
    return max(0.,1.5*integral/(pow(r,2.5)*(r-3.)))/.00017188421;
}
vec3 thermalPalette(float t){
    return mix(vec3(1.,.105,.018),mix(vec3(1.,.48,.13),vec3(1.,.91,.69),smoothstep(.62,1.3,t)),smoothstep(.2,.85,t));
}
// Three narrow wavelength channels of Planck B_nu (illustrative RGB response).
vec3 planck(float temperature){
    vec3 lambda=vec3(610.,550.,460.)*1.e-9;
    vec3 x=vec3(.0143877688)/(lambda*temperature);
    return pow(vec3(550.e-9)/lambda,vec3(3.))/(exp(clamp(x,vec3(0),vec3(80)))-1.)*.045;
}
vec3 emission(vec3 pos,float lookback,float bPhi,float observerR){
    float r=length(pos),f=flux(r);
    float omega=pow(r,-1.5);
    float g=useRedshift==1?sqrt(1.-3./r)/(sqrt(1.-2./observerR)*(1.+omega*bPhi)):1.;
    // bPhi belongs to the past-directed ray; future photon lambda = -bPhi.
    float angle=atan(pos.z,pos.x)-omega*(simTime-lookback);
    float textureGain=1.;
    if(useTexture==1){
        vec3 p=vec3(r*3.5,cos(angle)*r*.65,sin(angle)*r*.65);
        float turbulent=fbm(p);
        float filaments=valueNoise(vec3(r*11.,cos(angle)*2.,sin(angle)*2.));
        textureGain=(.25+1.5*turbulent)*(.8+.4*filaments);
    }
    float edge=1.-smoothstep(25.,28.,r);
    float intensity=f*textureGain;
    if(colorMode==1) return planck(82000.*pow(max(intensity,0.),.25)*g)*edge;
    if(colorMode==2) return (g<1.?mix(vec3(.08,.2,.9),vec3(.95),clamp((g-.4)/.6,0.,1.)):mix(vec3(.95),vec3(1.,.18,.035),clamp((g-1.)/.55,0.,1.)))*intensity*edge;
    float temp=pow(max(intensity,0.),.25)*g;
    return thermalPalette(temp)*intensity*pow(g,4.)*edge*1.25;
}
vec3 sky(vec3 d){
    vec3 color=vec3(.0005,.0008,.0015);
    if(showStars==1){
        float band=exp(-pow(dot(d,normalize(vec3(.23,.88,.42)))*6.,2.));
        float clouds=fbm(d*7.+vec3(9.));
        color+=vec3(.007,.008,.012)*band*pow(clouds,2.);
        vec2 coord=vec2(atan(d.z,d.x)/(2.*PI)+.5,asin(clamp(d.y,-1.,1.))/PI+.5)*vec2(520.,260.);
        vec2 cell=floor(coord),f=fract(coord);
        float seed=hash13(vec3(cell,8.));
        vec2 star=vec2(hash13(vec3(cell,2.)),hash13(vec3(cell,3.)))*.7+.15;
        float size=mix(.025,.08,pow(seed,8.));
        float light=exp(-dot(f-star,f-star)/(size*size));
        light*=step(.82,seed)*(.16+pow(seed,30.)*1.6);
        color+=mix(vec3(.65,.78,1.),vec3(1.,.83,.58),hash13(vec3(cell,6.)))*light;
    }
    if(showSkyGrid==1){
        vec2 a=vec2(atan(d.z,d.x),asin(clamp(d.y,-1.,1.)))*12./PI;
        vec2 gap=abs(fract(a+.5)-.5);
        float line=1.-smoothstep(.006,.014,min(gap.x,gap.y));
        color+=vec3(.023,.08,.10)*line;
    }
    return color;
}
void main(){
    vec2 pixelUV=gl_FragCoord.xy/resolution;
    if(validationMode==2)pixelUV.y=.5;
    vec2 screen=(pixelUV*2.-1.)*vec2(resolution.x/resolution.y,1.);
    // Precomputed tan(35 degrees / 2): avoid driver-specific tan approximations.
    vec3 direction=normalize(forward+.315298788879*(screen.x*right+screen.y*up));
    float observerR=length(eye);vec3 e0=eye/observerR;
    float radial=dot(direction,e0);
    vec3 tangent=direction-radial*e0;
    float sinAlpha=length(tangent);
    vec3 e1=tangent/max(sinAlpha,1.e-8);
    float b=observerR*sinAlpha/(useGR==1?sqrt(1.-2./observerR):1.);
    vec3 s=vec3(1./observerR,-radial/max(b,1.e-7),0.);
    if(validationMode==1){
        const float bs[8]=float[8](5.15,5.19,5.20,5.25,6.,10.,20.,50.);
        b=bs[int(gl_FragCoord.x)];observerR=2000.;s=vec3(1./observerR,sqrt(1./(b*b)-1./(observerR*observerR)+2./pow(observerR,3.)),0.);
    }
    if(b<.00001){outputColor=vec4(0,0,0,1);return;}
    float phi=0.;
    float crossing=atan(-e0.y,e1.y);if(crossing<=.00001)crossing+=PI;
    float bPhi=-b*cross(e0,e1).y;
    vec3 radiance=vec3(0);bool captured=false,escaped=false,hit=false;
    float error=0.;
    for(int i=0;i<1800;i++){
        // Angular cap plus a relative radial-change cap resolves close approaches
        // and prevents skipping across infinity. RK4 remains regular at turning points.
        float h=min(stepSize,.12*max(s.x,1.e-7)/max(abs(s.y),1.e-7));
        vec3 next=rk4(s,h,b);
        if(validationMode>0) error=max(error,abs((next.y*next.y+next.x*next.x-2.*next.x*next.x*next.x)*b*b-1.));
        if(validationMode==0 && showDisk==1 && phi+h>=crossing){
            vec3 at=rk4(s,crossing-phi,b);
            float r=1./at.x;
            if(r>=6. && r<=28.){
                vec3 pos=(e0*cos(crossing)+e1*sin(crossing))*r;
                radiance+=emission(pos,at.z,bPhi,observerR);hit=true;break;
            }
            crossing+=PI;
        }
        if(validationMode==0 && showHalo==1){
            float u=.5*(s.x+next.x),r=1./u,f=max(.0001,1.-2.*u);
            if(r>2.7 && r<16.){
                float y=(e0.y*cos(phi+.5*h)+e1.y*sin(phi+.5*h))*r;
                // Prescribed static, optically thin corona: bolometric g^4 j dl.
                float j=.002*exp(-pow((r-5.)/3.2,2.)-pow(y/(.55*r),2.))*smoothstep(2.7,3.4,r);
                float dl=h*r*r/(b*sqrt(f));
                float g2=useRedshift==1?f/(1.-2./observerR):1.;
                radiance+=vec3(1.,.28,.065)*j*dl*g2*g2;
            }
        }
        phi+=h;s=next;
        if(s.x>=.49995){captured=true;break;}
        if(s.x<.0005 && s.y<0.){phi+=atan(s.x,-s.y);escaped=true;break;}
    }
    if(validationMode==1){outputColor=vec4(captured?1.:0.,phi+asin(b/observerR)-PI,error,escaped?1.:0.);return;}
    if(validationMode==2){outputColor=vec4(captured?1.:0.,b,error,escaped?1.:0.);return;}
    if(escaped && !hit)radiance+=sky(normalize(e0*cos(phi)+e1*sin(phi)));
    outputColor=vec4(max(radiance,vec3(0.)),1.);
}
