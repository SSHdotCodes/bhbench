#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
const char *kQuadVertex = R"GLSL(
#version 410 core
layout(location=0) in vec2 aPosition;
void main(){gl_Position=vec4(aPosition,0.0,1.0);}
)GLSL";

const char *kRayFragment = R"GLSL(
#version 410 core
out vec4 FragColor;
uniform vec2 uResolution;
uniform vec3 uCamera,uForward,uRight,uUp;
uniform float uTime;
vec3 hash32(vec2 p){vec3 q=vec3(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3)),dot(p,vec2(419.2,371.9)));return fract(sin(q)*43758.5453);}
vec3 sky(vec3 d){
    vec2 uv=vec2(atan(d.y,d.x)*0.15915494+0.5,asin(clamp(d.z,-1.0,1.0))*0.31830989+0.5);
    vec2 grid=uv*vec2(880.0,440.0), cell=floor(grid), local=fract(grid);
    vec3 rnd=hash32(cell);float star=step(0.9965,rnd.z);
    float core=(1.0-smoothstep(0.006,0.035,length(local-rnd.xy)))*star;
    float halo=(1.0-smoothstep(0.015,0.11,length(local-rnd.xy)))*star*0.22;
    vec3 tint=mix(vec3(0.60,0.76,1.0),vec3(1.0,0.73,0.42),rnd.x);
    float milky=exp(-pow((d.z-0.12*sin(uv.x*6.283))/0.12,2.0));
    vec3 nebula=vec3(0.018,0.028,0.075)*milky*(0.55+0.45*sin(uv.x*93.0+uv.y*27.0));
    return vec3(0.0025,0.0045,0.013)+nebula+tint*(core*3.2+halo*0.7);
}
vec3 opticalAcceleration(vec3 p,vec3 v){
    float r=max(length(p),0.5001);
    // Exact grad(log n) for the Schwarzschild isotropic optical index, M=1.
    float dLogN=-1.5/(r*(r+0.5))-0.5/(r*(r-0.5));
    vec3 g=dLogN*p/r;return g-v*dot(g,v);
}
float opticalIndex(float r){return pow(1.0+0.5/r,3.0)/(1.0-0.5/r);}
vec3 diskEmission(vec3 hit,vec3 rayDirection,float t){
    float rho=length(hit.xy);const float inner=4.94949,outer=21.0;
    float edge=max(0.0,1.0-sqrt(inner/rho));
    float radial=edge*pow(inner/rho,2.65)*smoothstep(outer,outer-1.5,rho);
    float a=atan(hit.y,hit.x),spiral=a*5.0-log(rho)*11.0+t*0.48;
    float turbulence=(0.78+0.22*sin(spiral+0.7*sin(a*13.0-t*0.16)))*(0.90+0.10*sin(a*31.0+log(rho)*18.0-t*0.31));
    float areal=rho*pow(1.0+0.5/rho,2.0),v=sqrt(1.0/max(areal-2.0,0.01));
    float gamma=inversesqrt(max(1.0-v*v,0.03));
    vec3 tangent=normalize(vec3(-hit.y,hit.x,0.0));
    float toward=dot(tangent,-rayDirection),lapse=(1.0-0.5/rho)/(1.0+0.5/rho);
    float gShift=lapse/max(gamma*(1.0-v*toward),0.12),observed=pow(clamp(gShift,0.20,2.4),3.6);
    float temp=pow(max(radial,0.0),0.25);
    vec3 color=mix(vec3(1.0,0.19,0.035),vec3(1.0,0.68,0.28),smoothstep(0.06,0.42,temp));
    color=mix(color,vec3(0.78,0.88,1.0),smoothstep(0.42,0.83,temp));
    return color*(5.8*radial*turbulence*observed);
}
void main(){
    vec2 s=2.0*gl_FragCoord.xy/uResolution-1.0;s.x*=uResolution.x/uResolution.y;
    vec3 d=normalize(uForward+tan(radians(23.0))*(s.x*uRight+s.y*uUp)),d0=d,p=uCamera;
    float traveled=0.0;bool captured=false,hitDisk=false;vec3 color=vec3(0.0);
    for(int i=0;i<720;++i){
        float r=length(p);if(r<=0.515){captured=true;break;}if(r>=55.0&&traveled>7.0)break;
        float ds=clamp(r*0.021,0.014,0.25);
        vec3 a0=opticalAcceleration(p,d),vh=normalize(d+0.5*ds*a0);
        vec3 pn=p+ds*vh,am=opticalAcceleration(p+0.5*ds*vh,vh),dn=normalize(d+ds*am);
        pn=p+0.5*ds*(d+dn);
        if((p.z>0.0&&pn.z<=0.0)||(p.z<0.0&&pn.z>=0.0)){
            float f=clamp(p.z/(p.z-pn.z),0.0,1.0);vec3 hit=mix(p,pn,f);float rr=length(hit.xy);
            if(rr>=4.94949&&rr<=21.0){color=diskEmission(hit,normalize(mix(d,dn,f)),uTime);hitDisk=true;break;}
        }
        p=pn;d=dn;traveled+=ds;
    }
    if(!captured&&!hitDisk)color=sky(d);
    float camR=length(uCamera),b=opticalIndex(camR)*length(cross(uCamera,d0));
    float ring=exp(-pow((b-5.1961524)/0.20,2.0)),halo=exp(-pow((b-5.1961524)/0.62,2.0));
    if(captured)color=vec3(0.0);
    color+=vec3(1.0,0.30,0.075)*(0.52*ring+0.085*halo);
    float vignette=smoothstep(1.65,0.20,length(s));color*=0.72+0.28*vignette;
    color=color/(1.0+color*0.72);color=pow(max(color,vec3(0.0)),vec3(0.82));FragColor=vec4(color,1.0);
}
)GLSL";

const char *kLineVertex=R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;layout(location=1) in vec3 aColor;
uniform mat4 uMVP;out vec3 vColor;
void main(){vColor=aColor;gl_Position=uMVP*vec4(aPosition,1.0);}
)GLSL";
const char *kLineFragment=R"GLSL(
#version 410 core
in vec3 vColor;out vec4 FragColor;
void main(){FragColor=vec4(vColor,1.0);}
)GLSL";
const char *kOverlayVertex=R"GLSL(
#version 410 core
layout(location=0) in vec2 aPosition;layout(location=1) in vec4 aColor;
out vec4 vColor;void main(){vColor=aColor;gl_Position=vec4(aPosition,0.0,1.0);}
)GLSL";
const char *kOverlayFragment=R"GLSL(
#version 410 core
in vec4 vColor;out vec4 FragColor;
void main(){FragColor=vColor;}
)GLSL";
const char *kPresentFragment=R"GLSL(
#version 410 core
out vec4 FragColor;uniform sampler2D uFrame;uniform vec2 uResolution;
void main(){FragColor=texture(uFrame,gl_FragCoord.xy/uResolution);}
)GLSL";

GLuint compileShader(GLenum type,const char *src){
    GLuint s=glCreateShader(type);glShaderSource(s,1,&src,nullptr);glCompileShader(s);GLint ok=GL_FALSE;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){GLint n=0;glGetShaderiv(s,GL_INFO_LOG_LENGTH,&n);std::string log(static_cast<size_t>(std::max(n,1)),'\0');glGetShaderInfoLog(s,n,nullptr,log.data());glDeleteShader(s);throw std::runtime_error("shader compile failed: "+log);}return s;
}
GLuint makeProgram(const char *vs,const char *fs){
    GLuint v=compileShader(GL_VERTEX_SHADER,vs),f=compileShader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok=GL_FALSE;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GLint n=0;glGetProgramiv(p,GL_INFO_LOG_LENGTH,&n);std::string log(static_cast<size_t>(std::max(n,1)),'\0');glGetProgramInfoLog(p,n,nullptr,log.data());glDeleteProgram(p);throw std::runtime_error("program link failed: "+log);}return p;
}

struct Vertex3D{float x,y,z,r,g,b;};
void pushLine(std::vector<Vertex3D>&v,glm::vec3 a,glm::vec3 ca,glm::vec3 b,glm::vec3 cb){v.push_back({a.x,a.y,a.z,ca.r,ca.g,ca.b});v.push_back({b.x,b.y,b.z,cb.r,cb.g,cb.b});}
void addLine(std::vector<Vertex3D>&v,const std::vector<glm::vec3>&p,glm::vec3 c,bool closed=false){for(size_t i=1;i<p.size();++i)pushLine(v,p[i-1],c,p[i],c);if(closed&&p.size()>2)pushLine(v,p.back(),c,p.front(),c);}
std::vector<Vertex3D> makeFlammGrid(GLsizei &lineVertices){
    std::vector<Vertex3D> v;const glm::vec3 inner(0.96f,0.31f,0.12f),outer(0.13f,0.76f,0.91f);constexpr int n=240;
    // First batch: the Flamm embedding grid as concentric rings and radial coordinate lines.
    for(int ir=0;ir<=24;++ir){
        float r=2.03f+0.39f*ir,z=2.0f*std::sqrt(2.0f*(r-2.0f));glm::vec3 c=glm::mix(inner,outer,ir/24.0f);std::vector<glm::vec3> p;
        for(int j=0;j<n;++j){float a=2*kPi*j/n;p.emplace_back(r*std::cos(a),r*std::sin(a),z);}addLine(v,p,c,true);
    }
    for(int ia=0;ia<48;++ia){float a=2*kPi*ia/48.0f;std::vector<glm::vec3> p;
        for(int j=0;j<=90;++j){float r=2.03f+(11.4f-2.03f)*j/90.0f,z=2.0f*std::sqrt(2.0f*(r-2.0f));p.emplace_back(r*std::cos(a),r*std::sin(a),z);}
        addLine(v,p,glm::mix(inner,outer,static_cast<float>(ia%48)/47.0f));
    }
    std::vector<glm::vec3> rim;for(int j=0;j<n;++j){float a=2*kPi*j/n;rim.emplace_back(2.0f*std::cos(a),2.0f*std::sin(a),0.0f);}
    addLine(v,rim,glm::vec3(1.0f,0.47f,0.18f),true);lineVertices=static_cast<GLsizei>(v.size());
    // Dark floor below the throat reads as the mouth of the trapdoor.
    glm::vec3 c(0.014f,0.009f,0.022f),center(0,0,-0.08f);
    for(int j=0;j<n;++j){float a0=2*kPi*j/n,a1=2*kPi*(j+1)/n;glm::vec3 p0(1.99f*std::cos(a0),1.99f*std::sin(a0),-0.08f),p1(1.99f*std::cos(a1),1.99f*std::sin(a1),-0.08f);
        v.push_back({center.x,center.y,center.z,c.r,c.g,c.b});v.push_back({p0.x,p0.y,p0.z,c.r,c.g,c.b});v.push_back({p1.x,p1.y,p1.z,c.r,c.g,c.b});}
    return v;
}

std::array<uint8_t,7> glyph(char input){
    char c=(input>='a'&&input<='z')?static_cast<char>(input-'a'+'A'):input;
    switch(c){
        case 'A':return{14,17,17,31,17,17,17};case 'B':return{30,17,17,30,17,17,30};case 'C':return{14,17,16,16,16,17,14};case 'D':return{30,17,17,17,17,17,30};
        case 'E':return{31,16,16,30,16,16,31};case 'F':return{31,16,16,30,16,16,16};case 'G':return{14,17,16,23,17,17,14};case 'H':return{17,17,17,31,17,17,17};
        case 'I':return{14,4,4,4,4,4,14};case 'J':return{7,2,2,2,18,18,12};case 'K':return{17,18,20,24,20,18,17};case 'L':return{16,16,16,16,16,16,31};
        case 'M':return{17,27,21,21,17,17,17};case 'N':return{17,25,21,19,17,17,17};case 'O':return{14,17,17,17,17,17,14};case 'P':return{30,17,17,30,16,16,16};
        case 'Q':return{14,17,17,17,21,18,13};case 'R':return{30,17,17,30,20,18,17};case 'S':return{15,16,16,14,1,1,30};case 'T':return{31,4,4,4,4,4,4};
        case 'U':return{17,17,17,17,17,17,14};case 'V':return{17,17,17,17,17,10,4};case 'W':return{17,17,17,21,21,21,10};case 'X':return{17,17,10,4,10,17,17};
        case 'Y':return{17,17,10,4,4,4,4};case 'Z':return{31,1,2,4,8,16,31};case '0':return{14,17,19,21,25,17,14};case '1':return{4,12,4,4,4,4,14};
        case '2':return{14,17,1,2,4,8,31};case '3':return{30,1,1,14,1,1,30};case '4':return{2,6,10,18,31,2,2};case '5':return{31,16,16,30,1,1,30};
        case '6':return{14,16,16,30,17,17,14};case '7':return{31,1,2,4,8,8,8};case '8':return{14,17,17,14,17,17,14};case '9':return{14,17,17,15,1,1,14};
        case '-':return{0,0,0,31,0,0,0};case '+':return{0,4,4,31,4,4,0};case ':':return{0,4,4,0,4,4,0};case '.':return{0,0,0,0,0,12,12};
        case '/':return{1,2,2,4,8,8,16};case '=':return{0,31,0,31,0,0,0};case '(':return{2,4,8,8,8,4,2};case ')':return{8,4,2,2,2,4,8};
        case '<':return{2,4,8,16,8,4,2};case '>':return{8,4,2,1,2,4,8};case '*':return{0,21,14,31,14,21,0};case ' ':return{0,0,0,0,0,0,0};
        default:return{31,17,5,4,4,0,4};
    }
}
using Overlay=std::vector<float>;
void addRect(Overlay&o,float x,float y,float w,float h,int W,int H,glm::vec4 c){
    float x0=2*x/W-1,x1=2*(x+w)/W-1,y0=1-2*(y+h)/H,y1=1-2*y/H;float p[12]={x0,y0,x1,y0,x1,y1,x0,y0,x1,y1,x0,y1};
    for(int i=0;i<6;++i){o.push_back(p[i*2]);o.push_back(p[i*2+1]);o.push_back(c.r);o.push_back(c.g);o.push_back(c.b);o.push_back(c.a);}
}
void addText(Overlay&o,float x,float y,const std::string&s,int scale,int W,int H,glm::vec4 c){
    float cursor=x;for(char ch:s){auto rows=glyph(ch);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(rows[row]&(1u<<(4-col)))addRect(o,cursor+col*scale,y+row*scale,scale,scale,W,H,c);cursor+=6*scale;}
}

struct App{
    GLFWwindow*window=nullptr;GLuint rayProgram=0,lineProgram=0,overlayProgram=0,presentProgram=0;
    GLuint quadVao=0,quadVbo=0,gridVao=0,gridVbo=0,overlayVao=0,overlayVbo=0;
    GLuint fbo=0,colorTexture=0,depthBuffer=0;GLsizei gridLineVertices=0,gridFillVertices=0;int renderW=0,renderH=0;
    float yaw=0,elevation=0.285f,distance=15.5f,simulationTime=0;bool paused=false,showGrid=true;double previousFrame=0;
};
void keyCallback(GLFWwindow*w,int key,int,int action,int){if(action!=GLFW_PRESS)return;auto*a=static_cast<App*>(glfwGetWindowUserPointer(w));
    if(key==GLFW_KEY_ESCAPE)glfwSetWindowShouldClose(w,GLFW_TRUE);if(key==GLFW_KEY_SPACE)a->paused=!a->paused;if(key==GLFW_KEY_G)a->showGrid=!a->showGrid;
    if(key==GLFW_KEY_R){a->yaw=0;a->elevation=0.285f;a->distance=15.5f;}}

std::pair<int,int> internalResolution(int width,int height){
    float scale=std::min({0.50f,1100.0f/static_cast<float>(width),760.0f/static_cast<float>(height)});
    return {std::max(1,static_cast<int>(width*scale)),std::max(1,static_cast<int>(height*scale))};
}
void createFramebuffer(App&a,int width,int height){
    if(a.fbo){glDeleteFramebuffers(1,&a.fbo);glDeleteTextures(1,&a.colorTexture);glDeleteRenderbuffers(1,&a.depthBuffer);}
    auto [renderW,renderH]=internalResolution(width,height);a.renderW=renderW;a.renderH=renderH;
    glGenFramebuffers(1,&a.fbo);glBindFramebuffer(GL_FRAMEBUFFER,a.fbo);glGenTextures(1,&a.colorTexture);glBindTexture(GL_TEXTURE_2D,a.colorTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,a.renderW,a.renderH,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,a.colorTexture,0);glGenRenderbuffers(1,&a.depthBuffer);glBindRenderbuffer(GL_RENDERBUFFER,a.depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,a.renderW,a.renderH);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,a.depthBuffer);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("could not create render framebuffer");glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void initialize(App&a,int width,int height){
    a.rayProgram=makeProgram(kQuadVertex,kRayFragment);a.lineProgram=makeProgram(kLineVertex,kLineFragment);a.overlayProgram=makeProgram(kOverlayVertex,kOverlayFragment);a.presentProgram=makeProgram(kQuadVertex,kPresentFragment);
    const float q[]={-1,-1,1,-1,1,1,-1,-1,1,1,-1,1};glGenVertexArrays(1,&a.quadVao);glBindVertexArray(a.quadVao);glGenBuffers(1,&a.quadVbo);glBindBuffer(GL_ARRAY_BUFFER,a.quadVbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(q),q,GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
    auto grid=makeFlammGrid(a.gridLineVertices);a.gridFillVertices=static_cast<GLsizei>(grid.size())-a.gridLineVertices;
    glGenVertexArrays(1,&a.gridVao);glBindVertexArray(a.gridVao);glGenBuffers(1,&a.gridVbo);glBindBuffer(GL_ARRAY_BUFFER,a.gridVbo);glBufferData(GL_ARRAY_BUFFER,grid.size()*sizeof(Vertex3D),grid.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex3D),reinterpret_cast<void*>(0));glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex3D),reinterpret_cast<void*>(3*sizeof(float)));
    glGenVertexArrays(1,&a.overlayVao);glBindVertexArray(a.overlayVao);glGenBuffers(1,&a.overlayVbo);glBindBuffer(GL_ARRAY_BUFFER,a.overlayVbo);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(0));glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    glBindVertexArray(0);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);createFramebuffer(a,width,height);
}
void drawPanel(App&a,int W,int H){
    if(!a.showGrid)return;int x=static_cast<int>(W*0.675f),top=static_cast<int>(H*0.16f),w=W-x-static_cast<int>(W*0.025f),h=static_cast<int>(H*0.58f);
    glEnable(GL_SCISSOR_TEST);glScissor(x,H-top-h,w,h);glClearColor(0.004f,0.012f,0.027f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glViewport(x,H-top-h,w,h);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);float aspect=static_cast<float>(w)/h;
    glm::mat4 projection=glm::perspective(glm::radians(43.0f),aspect,0.1f,100.0f);
    glm::mat4 view=glm::lookAt(glm::vec3(13.5f,-17.0f,15.0f),glm::vec3(0,0,3.3f),glm::vec3(0,0,1));
    glm::mat4 model=glm::rotate(glm::mat4(1),0.09f*std::sin(a.simulationTime*0.15f),glm::vec3(0,0,1));glm::mat4 mvp=projection*view*model;
    glUseProgram(a.lineProgram);glUniformMatrix4fv(glGetUniformLocation(a.lineProgram,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));glBindVertexArray(a.gridVao);
    glDrawArrays(GL_TRIANGLES,a.gridLineVertices,a.gridFillVertices);glLineWidth(1.0f);glDrawArrays(GL_LINES,0,a.gridLineVertices);glBindVertexArray(0);glDisable(GL_DEPTH_TEST);
}
void renderOverlay(App&a){
    int W=a.renderW,H=a.renderH;Overlay v;glm::vec4 panel(0.006f,0.014f,0.030f,0.84f),white(0.79f,0.91f,1.0f,0.98f),muted(0.39f,0.66f,0.79f,0.98f),amber(1.0f,0.51f,0.20f,0.98f);
    addRect(v,12,12,255,42,W,H,panel);addText(v,20,18,"SCHWARZSCHILD",3,W,H,white);addText(v,21,41,"NULL GEODESIC LENSING",1,W,H,muted);
    int px=static_cast<int>(W*0.675f),py=static_cast<int>(H*0.16f),pw=W-px-static_cast<int>(W*0.025f);
    if(a.showGrid){addRect(v,px+7,py+7,pw-14,18,W,H,panel);addText(v,px+13,py+12,"SPACETIME GRID",2,W,H,white);
        addRect(v,px+7,py+static_cast<int>(H*0.58f)-30,pw-14,23,W,H,panel);addText(v,px+13,py+static_cast<int>(H*0.58f)-25,"FLAMM EMBEDDING",2,W,H,muted);addText(v,px+13,py+static_cast<int>(H*0.58f)-10,"HORIZON 2M  |  ISCO 6M",1,W,H,amber);}
    addRect(v,12,H-63,478,50,W,H,panel);addText(v,20,H-56,"ARROWS ORBIT  +/- ZOOM  SPACE PAUSE",2,W,H,white);addText(v,20,H-35,"R RESET   G GRID   ESC QUIT",2,W,H,muted);
    addRect(v,12,H-100,277,27,W,H,panel);addText(v,20,H-94,"HORIZON 2M  PHOTON 3M  ISCO 6M",1,W,H,amber);
    glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(a.overlayProgram);glBindVertexArray(a.overlayVao);glBindBuffer(GL_ARRAY_BUFFER,a.overlayVbo);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()/6));glBindVertexArray(0);
}
void drawFrame(App&a,double now){
    int fbW=0,fbH=0;glfwGetFramebufferSize(a.window,&fbW,&fbH);if(fbW<1||fbH<1)return;
    double dt=std::clamp(now-a.previousFrame,0.0,0.05);a.previousFrame=now;if(!a.paused)a.simulationTime+=static_cast<float>(dt);
    float move=static_cast<float>(dt);
    if(glfwGetKey(a.window,GLFW_KEY_LEFT)==GLFW_PRESS)a.yaw-=0.85f*move;if(glfwGetKey(a.window,GLFW_KEY_RIGHT)==GLFW_PRESS)a.yaw+=0.85f*move;
    if(glfwGetKey(a.window,GLFW_KEY_UP)==GLFW_PRESS)a.elevation=std::min(1.0f,a.elevation+0.55f*move);if(glfwGetKey(a.window,GLFW_KEY_DOWN)==GLFW_PRESS)a.elevation=std::max(-0.05f,a.elevation-0.55f*move);
    if(glfwGetKey(a.window,GLFW_KEY_EQUAL)==GLFW_PRESS||glfwGetKey(a.window,GLFW_KEY_KP_ADD)==GLFW_PRESS)a.distance=std::max(8.5f,a.distance-8.0f*move);
    if(glfwGetKey(a.window,GLFW_KEY_MINUS)==GLFW_PRESS||glfwGetKey(a.window,GLFW_KEY_KP_SUBTRACT)==GLFW_PRESS)a.distance=std::min(28.0f,a.distance+8.0f*move);
    auto [targetW,targetH]=internalResolution(fbW,fbH);
    if(targetW!=a.renderW||targetH!=a.renderH)createFramebuffer(a,fbW,fbH);
    float ce=std::cos(a.elevation);glm::vec3 camera(a.distance*ce*std::sin(a.yaw),-a.distance*ce*std::cos(a.yaw),a.distance*std::sin(a.elevation));
    glm::vec3 forward=glm::normalize(-camera),right=glm::normalize(glm::cross(forward,glm::vec3(0,0,1))),up=glm::normalize(glm::cross(right,forward));
    glBindFramebuffer(GL_FRAMEBUFFER,a.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);glViewport(0,0,a.renderW,a.renderH);glDisable(GL_DEPTH_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(a.rayProgram);glUniform2f(glGetUniformLocation(a.rayProgram,"uResolution"),static_cast<float>(a.renderW),static_cast<float>(a.renderH));
    glUniform3fv(glGetUniformLocation(a.rayProgram,"uCamera"),1,glm::value_ptr(camera));glUniform3fv(glGetUniformLocation(a.rayProgram,"uForward"),1,glm::value_ptr(forward));
    glUniform3fv(glGetUniformLocation(a.rayProgram,"uRight"),1,glm::value_ptr(right));glUniform3fv(glGetUniformLocation(a.rayProgram,"uUp"),1,glm::value_ptr(up));glUniform1f(glGetUniformLocation(a.rayProgram,"uTime"),a.simulationTime);
    glBindVertexArray(a.quadVao);glDrawArrays(GL_TRIANGLES,0,6);glBindVertexArray(0);drawPanel(a,a.renderW,a.renderH);glViewport(0,0,a.renderW,a.renderH);renderOverlay(a);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,fbW,fbH);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(a.presentProgram);
    glUniform1i(glGetUniformLocation(a.presentProgram,"uFrame"),0);glUniform2f(glGetUniformLocation(a.presentProgram,"uResolution"),static_cast<float>(fbW),static_cast<float>(fbH));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,a.colorTexture);glBindVertexArray(a.quadVao);glDrawArrays(GL_TRIANGLES,0,6);glBindVertexArray(0);
}
} // namespace

int main(){
    if(!glfwInit()){std::cerr<<"could not initialize GLFW\n";return 1;}
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);glfwWindowHint(GLFW_RESIZABLE,GL_TRUE);glfwWindowHint(GLFW_SAMPLES,0);
    App app;app.window=glfwCreateWindow(1480,920,"Schwarzschild | Null Geodesic Ray Tracing",nullptr,nullptr);
    if(!app.window){std::cerr<<"could not create an OpenGL 4.1 window\n";glfwTerminate();return 1;}
    glfwMakeContextCurrent(app.window);glfwSwapInterval(1);glfwSetWindowUserPointer(app.window,&app);glfwSetKeyCallback(app.window,keyCallback);
    glewExperimental=GL_TRUE;GLenum ge=glewInit();if(ge!=GLEW_OK){std::cerr<<"GLEW: "<<glewGetErrorString(ge)<<"\n";glfwDestroyWindow(app.window);glfwTerminate();return 1;}glGetError();
    int w=0,h=0;glfwGetFramebufferSize(app.window,&w,&h);try{initialize(app,w,h);}catch(const std::exception&e){std::cerr<<e.what()<<"\n";glfwDestroyWindow(app.window);glfwTerminate();return 1;}
    app.previousFrame=glfwGetTime();while(!glfwWindowShouldClose(app.window)){drawFrame(app,glfwGetTime());glfwSwapBuffers(app.window);glfwPollEvents();}
    glDeleteProgram(app.rayProgram);glDeleteProgram(app.lineProgram);glDeleteProgram(app.overlayProgram);glDeleteProgram(app.presentProgram);glDeleteVertexArrays(1,&app.quadVao);glDeleteBuffers(1,&app.quadVbo);
    glDeleteVertexArrays(1,&app.gridVao);glDeleteBuffers(1,&app.gridVbo);glDeleteVertexArrays(1,&app.overlayVao);glDeleteBuffers(1,&app.overlayVbo);
    glDeleteFramebuffers(1,&app.fbo);glDeleteTextures(1,&app.colorTexture);glDeleteRenderbuffers(1,&app.depthBuffer);glfwDestroyWindow(app.window);glfwTerminate();return 0;
}
