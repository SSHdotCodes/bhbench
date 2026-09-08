#include "graphics.hpp"
#include "physics.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <iomanip>

std::string readFile(const std::string&path){std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open "+path);std::ostringstream s;s<<f.rdbuf();return s.str();}
GLuint program(const std::string&vert,const std::string&frag){
    auto compile=[](GLenum type,const std::string&code){GLuint shader=glCreateShader(type);const char*p=code.c_str();glShaderSource(shader,1,&p,nullptr);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);throw std::runtime_error(log);}return shader;};
    GLuint v=compile(GL_VERTEX_SHADER,vert),f=compile(GL_FRAGMENT_SHADER,frag),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[8192];glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log);}return p;
}
void uniform(GLuint p,const char*n,int v){glUniform1i(glGetUniformLocation(p,n),v);}
void uniform(GLuint p,const char*n,float v){glUniform1f(glGetUniformLocation(p,n),v);}
void uniform(GLuint p,const char*n,V3 v){glUniform3f(glGetUniformLocation(p,n),v.x,v.y,v.z);}
void Target::destroy(){if(depth)glDeleteRenderbuffers(1,&depth);if(texture)glDeleteTextures(1,&texture);if(fbo)glDeleteFramebuffers(1,&fbo);depth=texture=fbo=0;w=h=0;}
void Target::resize(int width,int height,bool withDepth){
    if(w==width && h==height)return;destroy();w=width;h=height;
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(withDepth){glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);}
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Framebuffer incomplete");glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void Renderer::init(){
    std::string root=std::string(BH_ROOT)+"/shaders/",fs=readFile(root+"fullscreen.vert");
    rayProgram=program(fs,readFile(root+"raytrace.frag"));postProgram=program(fs,readFile(root+"post.frag"));blurProgram=program(fs,readFile(root+"blur.frag"));meshProgram=program(readFile(root+"mesh.vert"),readFile(root+"mesh.frag"));lineProgram=program(readFile(root+"mesh.vert"),readFile(root+"line.frag"));
    glGenVertexArrays(1,&fullscreen);
    std::vector<V3> mesh;
    auto point=[](float t,float a){float r=2.f+28.f*t*t;return V3{r*std::cos(a),float(physics::embedding(r)-physics::embedding(30.)),r*std::sin(a)};};
    for(int i=0;i<120;i++)for(int j=0;j<240;j++){
        float t=float(i)/120,t2=float(i+1)/120,a=float(j)*2.f*physics::pi/240,a2=float(j+1)*2.f*physics::pi/240;
        V3 p=point(t,a),q=point(t2,a),r=point(t2,a2),s=point(t,a2);mesh.insert(mesh.end(),{p,q,r,p,r,s});
    }
    meshCount=int(mesh.size());glGenVertexArrays(1,&meshVAO);glGenBuffers(1,&meshVBO);glBindVertexArray(meshVAO);glBindBuffer(GL_ARRAY_BUFFER,meshVBO);glBufferData(GL_ARRAY_BUFFER,mesh.size()*sizeof(V3),mesh.data(),GL_STATIC_DRAW);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(V3),nullptr);glEnableVertexAttribArray(0);
    glGenVertexArrays(1,&lineVAO);glGenBuffers(1,&lineVBO);glBindVertexArray(lineVAO);glBindBuffer(GL_ARRAY_BUFFER,lineVBO);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(V3),nullptr);glEnableVertexAttribArray(0);
    for(double b:{4.6,5.12,5.195,5.205,5.5,6.5,8.5}){
        std::vector<V3> path;
        for(auto p:physics::path(b))path.push_back({float(p.r*std::cos(p.phi)),float(physics::embedding(p.r)-physics::embedding(30.)+.12),float(p.r*std::sin(p.phi))});
        paths.push_back(std::move(path));
    }
}
void Renderer::render(const SceneSettings&s,int w,int h){
    scene.resize(w,h);blur[0].resize(w/3,h/3);blur[1].resize(w/3,h/3);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glBindVertexArray(fullscreen);glBindFramebuffer(GL_FRAMEBUFFER,scene.fbo);glViewport(0,0,w,h);glUseProgram(rayProgram);
    float a=s.inclination*physics::pi/180.;V3 eye={s.distance*std::sin(a)*std::cos(s.azimuth),s.distance*std::cos(a),s.distance*std::sin(a)*std::sin(s.azimuth)};
    V3 f=norm(eye*-1.f),r=norm(cross(f,{0,1,0})),u=cross(r,f);
    uniform(rayProgram,"eye",eye);uniform(rayProgram,"forward",f);uniform(rayProgram,"right",r);uniform(rayProgram,"up",u);
    glUniform2f(glGetUniformLocation(rayProgram,"resolution"),float(w),float(h));
    uniform(rayProgram,"simTime",s.time);uniform(rayProgram,"stepSize",s.step);uniform(rayProgram,"validationMode",0);
    uniform(rayProgram,"showDisk",int(s.disk));uniform(rayProgram,"showHalo",int(s.halo));uniform(rayProgram,"showStars",int(s.stars));uniform(rayProgram,"showSkyGrid",int(s.skyGrid));uniform(rayProgram,"useGR",int(s.lensing));uniform(rayProgram,"useRedshift",int(s.redshift));uniform(rayProgram,"useTexture",int(s.texture));uniform(rayProgram,"colorMode",s.colorMode);
    glDrawArrays(GL_TRIANGLES,0,3);
    glUseProgram(blurProgram);uniform(blurProgram,"source",0);glActiveTexture(GL_TEXTURE0);
    for(int pass=0;pass<6;pass++){
        int dst=pass%2;glBindFramebuffer(GL_FRAMEBUFFER,blur[dst].fbo);glViewport(0,0,blur[dst].w,blur[dst].h);
        glBindTexture(GL_TEXTURE_2D,pass==0?scene.texture:blur[1-dst].texture);uniform(blurProgram,"extractBright",pass==0?1:0);
        glUniform2f(glGetUniformLocation(blurProgram,"direction"),pass%2==0?1.f/blur[dst].w:0.f,pass%2==1?1.f/blur[dst].h:0.f);glDrawArrays(GL_TRIANGLES,0,3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void Renderer::present(int x,int y,int w,int h,const SceneSettings&s){
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glViewport(x,y,w,h);glUseProgram(postProgram);glBindVertexArray(fullscreen);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene.texture);uniform(postProgram,"source",0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,blur[1].texture);uniform(postProgram,"bloomImage",1);
    uniform(postProgram,"exposure",s.exposure);uniform(postProgram,"bloomStrength",s.bloom?.28f:0.f);glDrawArrays(GL_TRIANGLES,0,3);glActiveTexture(GL_TEXTURE0);
}
void Renderer::lines(const std::vector<V3>&points,V3 color,const Mat4&mvp,bool strip,float alpha){
    glUseProgram(lineProgram);glUniformMatrix4fv(glGetUniformLocation(lineProgram,"mvp"),1,GL_FALSE,mvp.m);uniform(lineProgram,"color",color);uniform(lineProgram,"alpha",alpha);
    glBindVertexArray(lineVAO);glBindBuffer(GL_ARRAY_BUFFER,lineVBO);glBufferData(GL_ARRAY_BUFFER,points.size()*sizeof(V3),points.data(),GL_STREAM_DRAW);glDrawArrays(strip?GL_LINE_STRIP:GL_POINTS,0,int(points.size()));
}
void Renderer::embedding(int x,int y,int w,int h,float azimuth,float elevation,float distance,float time,bool rays){
    glEnable(GL_SCISSOR_TEST);glScissor(x,y,w,h);glViewport(x,y,w,h);glClearColor(.018f,.028f,.037f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    V3 center={0,-5,0},eye=center+V3{distance*std::cos(elevation)*std::cos(azimuth),distance*std::sin(elevation),distance*std::cos(elevation)*std::sin(azimuth)};
    Mat4 mvp=perspective(.68f,float(w)/h,.1f,250.f)*lookAt(eye,center);
    glUseProgram(meshProgram);glUniformMatrix4fv(glGetUniformLocation(meshProgram,"mvp"),1,GL_FALSE,mvp.m);glBindVertexArray(meshVAO);glDrawArrays(GL_TRIANGLES,0,meshCount);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(float radius:{2.01f,3.f,6.f}){
        std::vector<V3> ring;for(int j=0;j<=360;j++){float a=j*2.f*physics::pi/360.f;ring.push_back({radius*std::cos(a),float(physics::embedding(radius)-physics::embedding(30.)+.07),radius*std::sin(a)});}
        lines(ring,radius<2.1?V3{1,.38f,.15f}:(radius<4?V3{.35f,.75f,.86f}:V3{.9f,.65f,.30f}),mvp);
    }
    if(rays){
        for(size_t i=0;i<paths.size();i++){
            V3 c=i<3?V3{.95f,.42f,.17f}:V3{.42f,.84f,.72f};lines(paths[i],c,mvp,true,.65f);
            if(!paths[i].empty()){size_t index=size_t(std::fmod(time*.55f+float(i)*.13f,1.f)*float(paths[i].size()));glPointSize(5.f);lines({paths[i][index]},c,mvp,false,1.f);}
        }
    }
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
}
bool Renderer::validateGPU(std::string&report){
    Target target;target.resize(8,1);glBindTexture(GL_TEXTURE_2D,target.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,8,1,0,GL_RGBA,GL_FLOAT,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glViewport(0,0,8,1);glBindVertexArray(fullscreen);glUseProgram(rayProgram);
    uniform(rayProgram,"eye",V3{42,0,0});uniform(rayProgram,"forward",V3{-1,0,0});uniform(rayProgram,"right",V3{0,0,-1});uniform(rayProgram,"up",V3{0,1,0});glUniform2f(glGetUniformLocation(rayProgram,"resolution"),8,1);
    uniform(rayProgram,"useGR",1);uniform(rayProgram,"validationMode",1);bool ok=true;
    std::ostringstream out;out<<"GPU / CPU null-geodesic comparison (OpenGL RGBA32F readback)\n"<<std::setprecision(8);
    double bs[]={5.15,5.19,5.20,5.25,6.,10.,20.,50.};
    for(float step:{.045f,.025f,.012f}){
      uniform(rayProgram,"stepSize",step);glDrawArrays(GL_TRIANGLES,0,3);glFinish();float values[32];glReadPixels(0,0,8,1,GL_RGBA,GL_FLOAT,values);
      out<<"Angular step cap: "<<step<<" rad\n";
      for(int i=0;i<8;i++){
        auto cpu=physics::trace(bs[i]);bool captured=values[4*i]>.5f,escaped=values[4*i+3]>.5f;
        double error=std::abs(values[4*i+1]-(cpu.angle-physics::pi));
        bool pass=std::isfinite(values[4*i+2])&&captured==cpu.captured&&escaped==cpu.escaped&&values[4*i+2]<.00015&&(!escaped||error<.004);
        ok=ok&&pass;out<<(pass?"PASS":"FAIL")<<" b="<<bs[i]<<" captured="<<captured<<" escaped="<<escaped<<" deflection_error="<<error<<" rad, invariant_error="<<values[4*i+2]<<"\n";
      }
    }
    // Exercise the actual perspective camera / local tetrad, not just seeded b.
    target.resize(1000,1);glBindTexture(GL_TEXTURE_2D,target.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1000,1,0,GL_RGBA,GL_FLOAT,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glViewport(0,0,1000,1);
    uniform(rayProgram,"eye",V3{65,0,0});glUniform2f(glGetUniformLocation(rayProgram,"resolution"),1000,1000);uniform(rayProgram,"validationMode",2);uniform(rayProgram,"stepSize",.025f);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
    std::vector<float>mask(4000);glReadPixels(0,0,1000,1,GL_RGBA,GL_FLOAT,mask.data());int incorrect=0;double worstImpactError=0.;int worstIndex=0;
    for(int i=0;i<1000;i++){
        double screen=2.*(i+.5)/1000.-1.,alpha=std::atan(std::tan(35.*physics::pi/360.)*screen),expectedB=65.*std::abs(std::sin(alpha))/std::sqrt(1.-2./65.);
        if(std::abs(expectedB-mask[4*i+1])>worstImpactError){worstImpactError=std::abs(expectedB-mask[4*i+1]);worstIndex=i;}
        if((mask[4*i]>.5)!=(expectedB<physics::criticalImpact()) || (mask[4*i]>.5)==(mask[4*i+3]>.5))incorrect++;
    }
    bool cameraPass=incorrect==0&&worstImpactError<.0001;ok=ok&&cameraPass;out<<(cameraPass?"PASS":"FAIL")<<" Perspective camera: "<<incorrect<<" / 1000 capture mismatches; max impact error="<<worstImpactError<<" M at pixel "<<worstIndex<<", b="<<mask[4*worstIndex+1]<<"\n";
    uniform(rayProgram,"validationMode",0);glBindFramebuffer(GL_FRAMEBUFFER,0);target.destroy();report=out.str();return ok;
}
Renderer::~Renderer(){
    scene.destroy();for(auto&t:blur)t.destroy();for(GLuint p:{rayProgram,postProgram,blurProgram,meshProgram,lineProgram})if(p)glDeleteProgram(p);
    for(GLuint v:{fullscreen,meshVAO,lineVAO})if(v)glDeleteVertexArrays(1,&v);for(GLuint b:{meshVBO,lineVBO})if(b)glDeleteBuffers(1,&b);
}
