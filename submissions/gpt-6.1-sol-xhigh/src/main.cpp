#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "physics.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
fs::path shaderRoot=fs::path(BH_SOURCE_DIR)/"shaders";
fs::path projectRoot=BH_SOURCE_DIR;
struct V3 {
    float x,y,z;
    V3 operator+(V3 b)const{return{x+b.x,y+b.y,z+b.z};}
    V3 operator-(V3 b)const{return{x-b.x,y-b.y,z-b.z};}
    V3 operator*(float a)const{return{x*a,y*a,z*a};}
};
float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V3 cross(V3 a,V3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V3 unit(V3 a){return a*(1/std::sqrt(dot(a,a)));}
struct Color {float r,g,b,a=1;};
struct Vertex {V3 p;Color c;};
using Mat=std::array<float,16>;
Mat multiply(Mat a,Mat b) {
    Mat c{}; for(int j=0;j<4;++j)for(int i=0;i<4;++i)for(int k=0;k<4;++k)c[j*4+i]+=a[k*4+i]*b[j*4+k]; return c;
}
Mat perspective(float aspect) {
    float f=1/(std::tan(.48f)*std::max(1.f,1.4f/aspect));
    return{f/aspect,0,0,0,0,f,0,0,0,0,-1.002f,-1,0,0,-.2002f,0};
}
Mat lookAt(V3 eye,V3 target) {
    V3 f=unit(target-eye),r=unit(cross(f,{0,1,0})),u=cross(r,f);
    return{r.x,u.x,-f.x,0,r.y,u.y,-f.y,0,r.z,u.z,-f.z,0,-dot(r,eye),-dot(u,eye),dot(f,eye),1};
}
std::string loadText(const std::string& file) {
    std::ifstream f(shaderRoot/file);
    if(!f)throw std::runtime_error("Cannot read shader "+file);
    return{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
GLuint compile(GLenum type,const std::string& source) {
    GLuint s=glCreateShader(type);const char* p=source.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[8192];glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log);}return s;
}
GLuint program(const std::string& vert,const std::string& frag) {
    GLuint a=compile(GL_VERTEX_SHADER,loadText(vert)),b=compile(GL_FRAGMENT_SHADER,loadText(frag)),p=glCreateProgram();
    glAttachShader(p,a);glAttachShader(p,b);glLinkProgram(p);glDeleteShader(a);glDeleteShader(b);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[8192];glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log);}return p;
}
void uniform(GLuint p,const char* n,int v){glUniform1i(glGetUniformLocation(p,n),v);}
void uniform(GLuint p,const char* n,float v){glUniform1f(glGetUniformLocation(p,n),v);}
void uniform(GLuint p,const char* n,V3 v){glUniform3f(glGetUniformLocation(p,n),v.x,v.y,v.z);}
void vec2(GLuint p,const char* n,float x,float y){glUniform2f(glGetUniformLocation(p,n),x,y);}
struct Target {
    GLuint fbo=0,texture=0;int w=0,h=0;
    void resize(int nw,int nh) {
        if(w==nw && h==nh)return;
        if(!fbo)glGenFramebuffers(1,&fbo);if(!texture)glGenTextures(1,&texture);w=nw;h=nh;
        glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Framebuffer incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    void destroy(){if(texture)glDeleteTextures(1,&texture);if(fbo)glDeleteFramebuffers(1,&fbo);}
};
// Original compact bitmap typeface. Columns are encoded as seven-bit rows, top first.
const std::string characters="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:/+-()[]=<>_?";
const std::array<std::array<unsigned char,7>,50> glyphs={{
{{14,17,17,31,17,17,17}},{{30,17,17,30,17,17,30}},{{14,17,16,16,16,17,14}},{{30,17,17,17,17,17,30}},
{{31,16,16,30,16,16,31}},{{31,16,16,30,16,16,16}},{{14,17,16,23,17,17,15}},{{17,17,17,31,17,17,17}},
{{14,4,4,4,4,4,14}},{{7,2,2,2,18,18,12}},{{17,18,20,24,20,18,17}},{{16,16,16,16,16,16,31}},
{{17,27,21,21,17,17,17}},{{17,25,21,19,17,17,17}},{{14,17,17,17,17,17,14}},{{30,17,17,30,16,16,16}},
{{14,17,17,17,21,18,13}},{{30,17,17,30,20,18,17}},{{15,16,16,14,1,1,30}},{{31,4,4,4,4,4,4}},
{{17,17,17,17,17,17,14}},{{17,17,17,17,17,10,4}},{{17,17,17,21,21,21,10}},{{17,17,10,4,10,17,17}},
{{17,17,10,4,4,4,4}},{{31,1,2,4,8,16,31}},
{{14,17,19,21,25,17,14}},{{4,12,4,4,4,4,14}},{{14,17,1,2,4,8,31}},{{30,1,1,14,1,1,30}},
{{2,6,10,18,31,2,2}},{{31,16,16,30,1,1,30}},{{14,16,16,30,17,17,14}},{{31,1,2,4,8,8,8}},
{{14,17,17,14,17,17,14}},{{14,17,17,15,1,1,14}},
{{0,0,0,0,0,12,12}},{{0,0,0,0,4,4,8}},{{0,4,4,0,4,4,0}},{{1,2,2,4,8,8,16}},
{{0,4,4,31,4,4,0}},{{0,0,0,31,0,0,0}},{{2,4,8,8,8,4,2}},{{8,4,2,2,2,4,8}},
{{14,8,8,8,8,8,14}},{{14,2,2,2,2,2,14}},{{0,0,31,0,31,0,0}},{{2,4,8,16,8,4,2}},
{{8,4,2,1,2,4,8}},{{0,0,0,0,0,0,31}}
}};
struct Button {float x,y,w,h;int action;};
struct App {
    GLFWwindow* window=nullptr;
    GLuint ray=0,post=0,blur=0,geom=0,vao=0,vbo=0,emptyVao=0;
    std::array<GLuint,4> queries{};std::array<bool,4> pending{};int queryIndex=0;
    Target scene,blurA,blurB;
    int width=1440,height=900,fbw=0,fbh=0,mode=0,color=0,quality=1,frames=0;
    bool disk=true,lensing=true,bloom=true,paused=false,orbit=false,texture=true,hud=true,drag=false,adaptive=true,captureRequested=false;
    float yaw=.25f,elevation=.24f,distance=52,exposure=.75f,renderScale=.7f,gpuMs=0,simTime=0,fps=0;
    double lastX=0,lastY=0,lastTime=0,fpsTime=0;int fpsFrames=0;
    std::vector<Vertex> ui,grid,surface,rings;
    std::vector<Button> buttons;
    std::string status="DRAG TO ORBIT / SCROLL TO ZOOM";
    V3 eye,forward,right,up;
    void init(bool hidden) {
        if(!glfwInit())throw std::runtime_error("GLFW failed to initialize");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
        glfwWindowHint(GLFW_VISIBLE,hidden?GL_FALSE:GL_TRUE);glfwWindowHint(GLFW_DEPTH_BITS,24);
        window=glfwCreateWindow(width,height,"Black Hole / Schwarzschild Relativity Lab",nullptr,nullptr);
        if(!window)throw std::runtime_error("OpenGL 4.1 window creation failed");
        glfwMakeContextCurrent(window);glfwSwapInterval(1);glfwSetWindowSizeLimits(window,1080,880,GLFW_DONT_CARE,GLFW_DONT_CARE);
        glfwSetWindowUserPointer(window,this);
        glfwSetKeyCallback(window,[](GLFWwindow* w,int k,int,int action,int){if(action==GLFW_PRESS)static_cast<App*>(glfwGetWindowUserPointer(w))->key(k);});
        glfwSetScrollCallback(window,[](GLFWwindow* w,double,double y){auto* a=static_cast<App*>(glfwGetWindowUserPointer(w));a->distance=std::clamp(a->distance*float(std::exp(-y*.08)),26.f,100.f);});
        glfwSetMouseButtonCallback(window,[](GLFWwindow* w,int b,int act,int){if(b==GLFW_MOUSE_BUTTON_LEFT)static_cast<App*>(glfwGetWindowUserPointer(w))->mouse(act);});
        glfwSetCursorPosCallback(window,[](GLFWwindow* w,double x,double y){auto* a=static_cast<App*>(glfwGetWindowUserPointer(w));if(a->drag){a->yaw-=float(x-a->lastX)*.005f;a->elevation=std::clamp(a->elevation+float(y-a->lastY)*.004f,-1.4f,1.4f);}a->lastX=x;a->lastY=y;});
        ray=program("fullscreen.vert","raytrace.frag");post=program("fullscreen.vert","post.frag");blur=program("fullscreen.vert","blur.frag");geom=program("geometry.vert","geometry.frag");
        glGenVertexArrays(1,&emptyVao);glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(sizeof(V3)));
        glGenQueries(4,queries.data());glEnable(GL_PROGRAM_POINT_SIZE);makeGrid();updateCamera();
        std::cout<<"OpenGL: "<<glGetString(GL_VERSION)<<"\nGPU: "<<glGetString(GL_RENDERER)<<'\n'<<std::flush;
        lastTime=fpsTime=glfwGetTime();if(!hidden){glfwShowWindow(window);glfwFocusWindow(window);}
    }
    void updateCamera() {
        eye={distance*std::cos(elevation)*std::sin(yaw),distance*std::sin(elevation),distance*std::cos(elevation)*std::cos(yaw)};
        forward=unit(eye*-1);right=unit(cross(forward,{0,1,0}));up=cross(right,forward);
    }
    void key(int k) {
        if(k==GLFW_KEY_ESCAPE)glfwSetWindowShouldClose(window,1);
        if(k==GLFW_KEY_1)mode=0;if(k==GLFW_KEY_2 || k==GLFW_KEY_G)mode=1;if(k==GLFW_KEY_3)mode=2;
        if(k==GLFW_KEY_SPACE)paused=!paused;if(k==GLFW_KEY_O)orbit=!orbit;
        if(k==GLFW_KEY_D)disk=!disk;if(k==GLFW_KEY_L)lensing=!lensing;
        if(k==GLFW_KEY_B)bloom=!bloom;if(k==GLFW_KEY_C)color=(color+1)%3;
        if(k==GLFW_KEY_T)texture=!texture;if(k==GLFW_KEY_H)hud=!hud;
        if(k==GLFW_KEY_Q){quality=(quality+1)%3;renderScale=quality==0?.5f:(quality==1?.7f:1.f);adaptive=quality!=2;}
        if(k==GLFW_KEY_EQUAL || k==GLFW_KEY_KP_ADD)exposure=std::min(8.f,exposure*1.15f);
        if(k==GLFW_KEY_MINUS || k==GLFW_KEY_KP_SUBTRACT)exposure=std::max(.1f,exposure/1.15f);
        if(k==GLFW_KEY_LEFT)yaw+=.06f;if(k==GLFW_KEY_RIGHT)yaw-=.06f;
        if(k==GLFW_KEY_UP)elevation=std::min(1.4f,elevation+.05f);if(k==GLFW_KEY_DOWN)elevation=std::max(-1.4f,elevation-.05f);
        if(k==GLFW_KEY_R){yaw=.25f;elevation=.24f;distance=52;exposure=.75f;simTime=0;orbit=false;}
        if(k==GLFW_KEY_P)captureRequested=true;
    }
    void mouse(int action) {
        if(action==GLFW_RELEASE){drag=false;return;}
        double x,y;glfwGetCursorPos(window,&x,&y);
        for(auto b:buttons)if(x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h){key(b.action);return;}
        if(x<width-300 && y>90 && y<height-58){drag=true;lastX=x;lastY=y;}
    }
    void rayUniforms(int w,int h,bool diagnostics=false) {
        glUseProgram(ray);vec2(ray,"resolution",float(w),float(h));uniform(ray,"eye",eye);uniform(ray,"forward",forward);uniform(ray,"right",right);uniform(ray,"up",up);
        float fov=diagnostics?.40f:.40f*std::max(1.f,1.5f/(float(w)/h));
        uniform(ray,"tanHalfFov",fov);uniform(ray,"simTime",simTime);uniform(ray,"stepSize",quality==2?.010f:(quality==0?.035f:.022f));
        uniform(ray,"diskEnabled",int(disk));uniform(ray,"lensingEnabled",int(lensing));uniform(ray,"colorMode",color);uniform(ray,"turbulence",int(texture));uniform(ray,"diagnostic",int(diagnostics));
    }
    void fullscreen(){glBindVertexArray(emptyVao);glDrawArrays(GL_TRIANGLES,0,3);}
    void bindTexture(GLuint id,int unit){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,id);}
    void renderOptical(int x,int y,int w,int h) {
        int sw=std::max(64,int(w*renderScale)),sh=std::max(64,int(h*renderScale));
        scene.resize(sw,sh);blurA.resize(std::max(32,sw/2),std::max(32,sh/2));blurB.resize(blurA.w,blurA.h);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glBindFramebuffer(GL_FRAMEBUFFER,scene.fbo);glViewport(0,0,sw,sh);
        rayUniforms(sw,sh);fullscreen();
        glUseProgram(blur);uniform(blur,"image",0);
        glBindFramebuffer(GL_FRAMEBUFFER,blurA.fbo);glViewport(0,0,blurA.w,blurA.h);bindTexture(scene.texture,0);vec2(blur,"axis",1.f/sw,0);uniform(blur,"extractBright",1);fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER,blurB.fbo);bindTexture(blurA.texture,0);vec2(blur,"axis",0,1.f/blurA.h);uniform(blur,"extractBright",0);fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER,blurA.fbo);bindTexture(blurB.texture,0);vec2(blur,"axis",2.f/blurA.w,0);fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER,blurB.fbo);bindTexture(blurA.texture,0);vec2(blur,"axis",0,2.f/blurA.h);fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(x,y,w,h);glUseProgram(post);bindTexture(scene.texture,0);bindTexture(blurB.texture,1);
        uniform(post,"scene",0);uniform(post,"bloomTexture",1);uniform(post,"exposure",exposure);uniform(post,"bloomStrength",bloom?.35f:0.f);fullscreen();
    }
    void drawVertices(const std::vector<Vertex>& vertices,GLenum primitive,const Mat& mvp,float pointSize=1,bool round=false) {
        if(vertices.empty())return;glUseProgram(geom);glUniformMatrix4fv(glGetUniformLocation(geom,"mvp"),1,GL_FALSE,mvp.data());
        uniform(geom,"pointSize",pointSize);uniform(geom,"roundPoints",int(round));glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STREAM_DRAW);glDrawArrays(primitive,0,GLsizei(vertices.size()));
    }
    V3 embed(float r,float angle) {return{r*std::cos(angle),float(bh::embeddingHeight(r)-bh::embeddingHeight(30)),r*std::sin(angle)};}
    void makeGrid() {
        const Color minor{.11f,.29f,.35f,1},major{.20f,.51f,.57f,1},face{.014f,.035f,.050f,1};
        for(int j=0;j<36;++j){float a=float(j*bh::pi/18);for(int i=0;i<160;++i){float t=i/160.f,t1=(i+1)/160.f;float r=2+28*t*t,r1=2+28*t1*t1;grid.push_back({embed(r,a),j%3?minor:major});grid.push_back({embed(r1,a),j%3?minor:major});}}
        for(int i=0;i<25;++i){float r=2+28*float(i*i)/576.f;for(int j=0;j<192;++j){grid.push_back({embed(r,float(j*2*bh::pi/192)),i%4?minor:major});grid.push_back({embed(r,float((j+1)*2*bh::pi/192)),i%4?minor:major});}}
        for(int i=0;i<80;++i)for(int j=0;j<128;++j){float t=i/80.f,t1=(i+1)/80.f,r=2+28*t*t,r1=2+28*t1*t1,a=float(j*2*bh::pi/128),a1=float((j+1)*2*bh::pi/128);V3 p=embed(r,a),q=embed(r1,a),s=embed(r,a1),v=embed(r1,a1);for(auto x:{p,q,s,s,q,v})surface.push_back({x,face});}
        for(int k=0;k<3;++k){float r=k==0?2.f:(k==1?3.f:6.f);Color c=k==0?Color{.31f,.79f,.84f,1}:(k==1?Color{.97f,.39f,.15f,1}:Color{.95f,.73f,.35f,1});for(int j=0;j<256;++j){auto p=embed(r,float(j*2*bh::pi/256)),q=embed(r,float((j+1)*2*bh::pi/256));p.y+=.025f;q.y+=.025f;rings.push_back({p,c});rings.push_back({q,c});}}
    }
    void renderGrid(int x,int y,int w,int h,bool inset=false) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(x,y,w,h);glEnable(GL_SCISSOR_TEST);glScissor(x,y,w,h);glClearColor(.007f,.016f,.023f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);float a=yaw+.55f;
        float radius=distance*1.07f,angle=std::clamp(.35f+elevation*.7f,.1f,1.25f);
        V3 e={radius*std::cos(angle)*std::sin(a),radius*std::sin(angle),radius*std::cos(angle)*std::cos(a)};
        Mat matrix=multiply(perspective(float(w)/h),lookAt(e,{0,-5,0}));
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1,1);drawVertices(surface,GL_TRIANGLES,matrix);glDisable(GL_POLYGON_OFFSET_FILL);
        drawVertices(grid,GL_LINES,matrix);drawVertices(rings,GL_LINES,matrix);
        // Radial test particles released from rest: r=(R0/2)(1+cos eta), tau=sqrt(R0^3/8)(eta+sin eta).
        // Only the exterior part of their proper-time geodesic is drawn on the spatial embedding.
        std::vector<Vertex> particles;const float R0=24,etaH=std::acos(4.f/R0-1),factor=std::sqrt(R0*R0*R0/8),fallTime=factor*(etaH+std::sin(etaH));
        for(int j=0;j<12;++j){float tau=std::fmod(simTime*.55f+j*fallTime/12,fallTime),lo=0,hi=etaH;for(int k=0;k<18;++k){float mid=(lo+hi)/2;if(factor*(mid+std::sin(mid))<tau)lo=mid;else hi=mid;}float r=R0/2*(1+std::cos((lo+hi)/2));auto p=embed(r,float(j*2*bh::pi/12));p.y+=.18f;particles.push_back({p,{.65f,.96f,1,1}});}
        drawVertices(particles,GL_POINTS,matrix,inset?3.f:6.f,true);glDisable(GL_DEPTH_TEST);
    }
    void rect(float x,float y,float w,float h,Color c) {
        V3 a{x,y,0},b{x+w,y,0},d{x,y+h,0},e{x+w,y+h,0};for(auto p:{a,b,d,d,b,e})ui.push_back({p,c});
    }
    void text(float x,float y,const std::string& s,float size,Color c) {
        for(unsigned char ch:s){if(ch==' '){x+=size*6;continue;}auto index=characters.find(char(std::toupper(ch)));if(index!=std::string::npos && index<glyphs.size())for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(glyphs[index][row]&(1<<(4-col)))rect(x+col*size,y+row*size,size*.87f,size*.87f,c);x+=size*6;}
    }
    void button(float x,float y,float w,const std::string& label,int action,bool selected=false) {
        rect(x,y,w,28,selected?Color{.08f,.22f,.27f,1}:Color{.031f,.063f,.077f,1});
        if(selected)rect(x,y,2,28,{.3f,.86f,.92f,1});text(x+10,y+9,label,1.25f,selected?Color{.7f,.96f,1,1}:Color{.49f,.63f,.68f,1});buttons.push_back({x,y,w,28,action});
    }
    void label(float x,float y,const std::string& s){text(x,y,s,1.25f,{.40f,.58f,.64f,1});}
    void buildHud() {
        ui.clear();buttons.clear();if(!hud)return;
        Color bright{.81f,.91f,.93f,1},cyan{.36f,.83f,.88f,1},muted{.39f,.56f,.63f,1};
        rect(0,0,float(width),88,{.007f,.014f,.020f,1});rect(26,27,3,32,cyan);
        text(42,25,"BLACK HOLE",3.2f,bright);text(43,57,"SCHWARZSCHILD / RELATIVITY LAB",1.35f,muted);
        std::ostringstream perf;perf<<std::fixed<<std::setprecision(0)<<fps<<" FPS  /  "<<scene.w<<" X "<<scene.h;
        text(float(width-540),29,perf.str(),1.4f,cyan);text(float(width-540),51,"OPENGL 4.1 / GPU NULL GEODESICS",1.2f,muted);
        float x=float(width-300);
        // Leave the live inset uncovered by the UI background.
        rect(x,88,300,191,{.011f,.022f,.029f,1});rect(x,484,300,float(height-536),{.011f,.022f,.029f,1});
        rect(x,279,17,205,{.011f,.022f,.029f,1});rect(x+284,279,16,205,{.011f,.022f,.029f,1});
        rect(x,88,1,float(height-142),{.08f,.16f,.19f,1});x+=19;
        label(x,111,"OBSERVER / GEOMETRIZED UNITS");
        std::ostringstream pos;pos<<std::fixed<<std::setprecision(1)<<"R = "<<distance<<" M   I = "<<(90-elevation*180/bh::pi)<<" DEG";text(x,135,pos.str(),1.35f,bright);
        label(x,161,"HORIZON 2M / PHOTON SPHERE 3M");label(x,181,"ISCO 6M / SHADOW B = 5.196M");
        button(x,213,78,"1 LIGHT",GLFW_KEY_1,mode==0);button(x+85,213,78,"2 GRID",GLFW_KEY_2,mode==1);button(x+170,213,91,"3 SPLIT",GLFW_KEY_3,mode==2);
        label(x,265,mode==1?"LENSED DISK / LIVE CAMERA":"SPATIAL CURVATURE / FLAMM SLICE");
        label(x,505,"CYAN: HORIZON   ORANGE: 3M");label(x,525,"GOLD: ISCO / DOTS: RADIAL FALL");
        label(x,554,"GRID IS A CONSTANT-TIME SLICE.");label(x,574,"HEIGHT IS AN EMBEDDING AXIS.");label(x,594,"EXTERIOR ONLY: IT STOPS AT 2M.");
        button(x,626,125,disk?"D DISK ON":"D DISK OFF",GLFW_KEY_D,disk);button(x+135,626,126,lensing?"L GR ON":"L GR OFF",GLFW_KEY_L,lensing);
        button(x,664,125,bloom?"B BLOOM ON":"B BLOOM OFF",GLFW_KEY_B,bloom);button(x+135,664,126,texture?"T DETAIL ON":"T DETAIL OFF",GLFW_KEY_T,texture);
        const char* colors[]={"C BOLOMETRIC / FALSE COLOR","C FREQUENCY SHIFT / G","C PLANCK / 3 NARROW BANDS"};button(x,702,261,colors[color],GLFW_KEY_C);
        if(height>840){const char* qs[]={"Q PERFORMANCE / ADAPTIVE","Q BALANCED / ADAPTIVE","Q HIGH / NATIVE RESOLUTION"};button(x,740,261,qs[quality],GLFW_KEY_Q);label(x,783,"MODEL: A = 0 / THIN OPAQUE DISK");label(x,803,"NOT A MAGNETOFLUID SIMULATION");}
        float y=float(height-52);rect(0,y,float(width),52,{.008f,.017f,.023f,1});rect(0,y,float(width),1,{.08f,.17f,.20f,1});
        text(27,y+12,"DRAG OR ARROWS: ORBIT   SCROLL: ZOOM   SPACE: PAUSE   O: AUTO ORBIT",1.25f,muted);
        text(27,y+31,"R: RESET   +/-: EXPOSURE   P: CAPTURE   H: HIDE UI   ESC: CLOSE",1.15f,muted);
        text(float(width-300),y+18,paused?"PAUSED":(orbit?"LIVE / ORBITING":"LIVE / STATIC CAMERA"),1.3f,cyan);
        text(26,106,mode==1?"02 / SPATIAL EMBEDDING":(mode==2?"03 / LIGHT + GEOMETRY":"01 / RELATIVISTIC LIGHT"),1.25f,muted);
        if(!lensing)text(26,130,"GR DISABLED / EUCLIDEAN COMPARISON",1.3f,{1,.55f,.3f,1});
        if(mode==1){
            text(26,float(height-140),"Z = 2 SQRT[2M(R-2M)]",1.5f,cyan);
            text(26,float(height-114),"EXACT SPATIAL SLICE / EMBEDDING HEIGHT",1.2f,muted);
            // Future radial null cones in ingoing Painleve-Gullstrand time:
            // dr/dT = -sqrt(2M/r) +/- 1. Both branches point inward for r < 2M.
            float cx=float(width-744),cy=float(height-245);
            rect(cx,cy,415,168,{.012f,.026f,.036f,.96f});
            text(cx+14,cy+13,"CAUSAL TRAPDOOR / RADIAL LIGHT",1.35f,cyan);
            text(cx+14,cy+34,"DR/DT = -SQRT(2M/R) +/- 1",1.25f,muted);
            for(int i=0;i<3;++i){
                float r=i==0?4.f:(i==1?2.f:1.f),yy=cy+63+i*26;
                text(cx+14,yy-3,i==0?"R=4M":(i==1?"R=2M":"R=1M"),1.15f,bright);
                float origin=cx+248;
                rect(origin,yy-6,1,15,muted);
                for(int branch=0;branch<2;++branch){
                    float v=-std::sqrt(2/r)+(branch==0?-1.f:1.f),len=v*48;
                    Color c=branch==0?Color{.98f,.49f,.25f,1}:cyan;
                    float ay=yy+(branch==0?-3.f:5.f),end=origin+len;
                    rect(std::min(origin,end),ay,std::max(1.f,std::abs(len)),1.5f,c);
                    if(std::abs(len)>1){float sign=len>0?1.f:-1.f;ui.push_back({{end,ay+.75f,0},c});ui.push_back({{end-sign*6,ay-3,0},c});ui.push_back({{end-sign*6,ay+4.5f,0},c});}
                    else rect(origin-2,ay-2,4,4,c);
                }
                if(i==1)text(cx+293,yy-3,"OUT: ZERO",1.05f,muted);
                if(i==2)text(cx+293,yy-3,"BOTH IN",1.05f,cyan);
            }
            text(cx+14,cy+147,"INSIDE 2M, EVERY FUTURE LIGHT RAY FALLS.",1.1f,bright);
        }
        Mat ortho{2.f/width,0,0,0,0,-2.f/height,0,0,0,0,-1,0,-1,1,0,1};
        glViewport(0,0,fbw,fbh);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);drawVertices(ui,GL_TRIANGLES,ortho);glDisable(GL_BLEND);
    }
    void render() {
        glfwGetWindowSize(window,&width,&height);glfwGetFramebufferSize(window,&fbw,&fbh);if(fbw<1||fbh<1)return;
        updateCamera();float sx=float(fbw)/width,sy=float(fbh)/height;
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,fbw,fbh);glClearColor(.005f,.011f,.017f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(pending[queryIndex]){GLint ready=0;glGetQueryObjectiv(queries[queryIndex],GL_QUERY_RESULT_AVAILABLE,&ready);if(ready){GLuint64 ns;glGetQueryObjectui64v(queries[queryIndex],GL_QUERY_RESULT,&ns);gpuMs=gpuMs==0?float(ns/1e6):gpuMs*.9f+float(ns/1e6)*.1f;pending[queryIndex]=false;}}
        bool timed=!pending[queryIndex];if(timed)glBeginQuery(GL_TIME_ELAPSED,queries[queryIndex]);
        int mainW=int((width-300)*sx),mainY=int(53*sy),mainH=int((height-141)*sy);
        if(!hud){mainW=fbw;mainY=0;mainH=fbh;}
        if(mode==0)renderOptical(0,mainY,mainW,mainH);
        else if(mode==1)renderGrid(0,mainY,mainW,mainH);
        else{int w=int(mainW*.58);renderOptical(0,mainY,w,mainH);renderGrid(w,mainY,mainW-w,mainH);}
        if(hud){int ix=int((width-283)*sx),iy=int((height-484)*sy),iw=int(267*sx),ih=int(205*sy);if(mode==1)renderOptical(ix,iy,iw,ih);else renderGrid(ix,iy,iw,ih,true);}
        if(timed){glEndQuery(GL_TIME_ELAPSED);pending[queryIndex]=true;}queryIndex=(queryIndex+1)%4;
        buildHud();
        if(captureRequested){saveCapture();captureRequested=false;}
        if(adaptive && frames%45==0 && frames>90 && mode!=1 && gpuMs>0){if(gpuMs>27)renderScale=std::max(.30f,renderScale*.92f);else if(gpuMs<18)renderScale=std::min(quality==0?.6f:.9f,renderScale*1.04f);}
        ++frames;
    }
    void saveCapture(const fs::path& explicitPath={}) {
        fs::path file=explicitPath.empty()?projectRoot/"captures"/("black-hole-"+std::to_string(std::time(nullptr))+".ppm"):explicitPath;
        fs::create_directories(file.parent_path());std::vector<unsigned char> pixels(size_t(fbw)*fbh*3);
        glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,fbw,fbh,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream out(file,std::ios::binary);out<<"P6\n"<<fbw<<' '<<fbh<<"\n255\n";for(int y=fbh-1;y>=0;--y)out.write(reinterpret_cast<char*>(pixels.data()+size_t(y)*fbw*3),fbw*3);
        status="CAPTURE SAVED";std::cout<<"Capture: "<<file<<'\n'<<std::flush;
    }
    int validateGpu() {
        bool savedDisk=disk;disk=false;quality=2;const int w=80,h=60;scene.resize(w,h);glBindFramebuffer(GL_FRAMEBUFFER,scene.fbo);glViewport(0,0,w,h);rayUniforms(w,h,true);fullscreen();
        std::vector<float> pixels(w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_FLOAT,pixels.data());int mismatch=0,compared=0;double maxAngle=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){float px=(2*(x+.5f)/w-1)*float(w)/h*.40f,py=(2*(y+.5f)/h-1)*.40f;V3 d=unit(forward+right*px+up*py);double nr=dot(d,unit(eye));double b=bh::impact(distance,nr);auto cpu=bh::trace(distance,nr,.002);int index=(y*w+x)*4;int gpuStatus=int(std::round(pixels[index+3]));if(std::abs(b-bh::criticalImpact)<.015)continue;++compared;if(gpuStatus!=cpu.status)++mismatch;if(gpuStatus==1&&cpu.status==1)maxAngle=std::max(maxAngle,std::abs(pixels[index+2]-cpu.phi));}
        std::cout<<"GPU/CPU rays compared: "<<compared<<"\nCapture/escape mismatches: "<<mismatch<<"\nMaximum escaped-ray angle difference: "<<maxAngle<<" rad\n";
        disk=true;rayUniforms(w,h,true);fullscreen();glReadPixels(0,0,w,h,GL_RGBA,GL_FLOAT,pixels.data());
        int hitCount=0,diskMismatch=0;double maxRadius=0,maxShift=0,maxTime=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float px=(2*(x+.5f)/w-1)*float(w)/h*.40f,py=(2*(y+.5f)/h-1)*.40f;
            V3 d=unit(forward+right*px+up*py),er=unit(eye);
            double nr=dot(d,er),b=bh::impact(distance,nr);
            V3 et=unit(d-er*float(nr)),normal=cross(er,et);
            auto hit=bh::traceDisk(distance,nr,er.y,et.y);
            int index=(y*w+x)*4,status=int(std::round(pixels[index+3]));
            if(std::abs(b-bh::criticalImpact)<.015)continue;
            if(status!=hit.status){++diskMismatch;continue;}
            if(status==3) {
                ++hitCount;
                double g=bh::frequencyShift(hit.radius,distance,-b*normal.y);
                maxRadius=std::max(maxRadius,std::abs(1.0/pixels[index]/hit.radius-1));
                maxShift=std::max(maxShift,std::abs(pixels[index+1]/g-1));
                maxTime=std::max(maxTime,std::abs(pixels[index+2]/hit.lookback-1));
            }
        }
        disk=savedDisk;glBindFramebuffer(GL_FRAMEBUFFER,0);
        std::cout<<"Disk intersections compared: "<<hitCount<<"\nDisk visibility mismatches: "<<diskMismatch
                 <<"\nMaximum relative disk radius error: "<<maxRadius<<"\nMaximum relative frequency shift error: "<<maxShift
                 <<"\nMaximum relative light travel time error: "<<maxTime<<'\n';
        return mismatch==0&&maxAngle<.01&&diskMismatch==0&&maxRadius<.003&&maxShift<.003&&maxTime<.003?0:1;
    }
    void destroy() {
        scene.destroy();blurA.destroy();blurB.destroy();glDeleteProgram(ray);glDeleteProgram(post);glDeleteProgram(blur);glDeleteProgram(geom);
        glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteVertexArrays(1,&emptyVao);glDeleteQueries(4,queries.data());glfwDestroyWindow(window);glfwTerminate();
    }
};
int main(int argc,char** argv) {
    fs::path executable=fs::absolute(argv[0]).parent_path();
    fs::path bundled=executable/".."/"Resources"/"shaders";
    if(fs::exists(bundled)){shaderRoot=fs::weakly_canonical(bundled);projectRoot=fs::weakly_canonical(executable/".."/".."/"..");}
    else if(fs::exists(executable/".."/"shaders")){projectRoot=fs::weakly_canonical(executable/"..");shaderRoot=projectRoot/"shaders";}
    bool validate=false,hidden=false;double seconds=0;std::string capture;int initialMode=0;
    for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--validate-gpu"){validate=true;hidden=true;}else if(a=="--hidden")hidden=true;else if(a=="--seconds"&&i+1<argc)seconds=std::stod(argv[++i]);else if(a=="--capture"&&i+1<argc)capture=argv[++i];else if(a=="--grid")initialMode=1;else if(a=="--split")initialMode=2;else if(a=="--help"){std::cout<<"black_hole [--grid|--split] [--seconds N] [--capture PATH.ppm] [--validate-gpu]\n";return 0;}else{std::cerr<<"Unknown argument: "<<a<<'\n';return 2;}}
    try {
        App app;app.mode=initialMode;app.init(hidden);if(validate){int result=app.validateGpu();app.destroy();return result;}
        double started=glfwGetTime();bool captured=false;
        while(!glfwWindowShouldClose(app.window)){
            double now=glfwGetTime(),dt=std::min(.1,now-app.lastTime);app.lastTime=now;
            if(!app.paused)app.simTime+=float(dt*8);if(app.orbit)app.yaw+=float(dt*.055);
            glfwPollEvents();app.render();
            if(!capture.empty()&&!captured&&now-started>2){app.saveCapture(capture);captured=true;}
            glfwSwapBuffers(app.window);++app.fpsFrames;
            if(now-app.fpsTime>=1){app.fps=float(app.fpsFrames/(now-app.fpsTime));app.fpsFrames=0;app.fpsTime=now;}
            if(seconds>0&&now-started>=seconds)break;
        }
        std::cout<<"Measured display FPS: "<<app.fps<<"\nGPU render time (smoothed): "<<app.gpuMs<<" ms\nInternal ray resolution: "<<app.scene.w<<" x "<<app.scene.h<<'\n';
        app.destroy();return 0;
    }catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<'\n';glfwTerminate();return 1;}
}
