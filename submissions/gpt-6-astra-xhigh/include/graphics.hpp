#pragma once
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "math.hpp"
#include <string>
#include <vector>
GLuint program(const std::string&vert,const std::string&frag);
std::string readFile(const std::string&path);
void uniform(GLuint p,const char*name,int v);
void uniform(GLuint p,const char*name,float v);
void uniform(GLuint p,const char*name,V3 v);
struct Target {
    GLuint fbo=0,texture=0,depth=0;int w=0,h=0;
    void resize(int width,int height,bool withDepth=false);
    void destroy();
};
struct SceneSettings {
    float inclination=83.,azimuth=.25f,distance=65.,time=0,exposure=1.15f;
    float step=.025f;int colorMode=0;
    bool disk=true,lensing=true,halo=true,stars=true,skyGrid=false,bloom=true,redshift=true,texture=true;
};
class Renderer {
public:
    void init();void render(const SceneSettings&s,int w,int h);
    void present(int x,int y,int w,int h,const SceneSettings&s);
    void embedding(int x,int y,int w,int h,float azimuth,float elevation,float distance,float time,bool rays);
    bool validateGPU(std::string&report);
    int width()const{return scene.w;} int height()const{return scene.h;}
    ~Renderer();
private:
    GLuint fullscreen=0,rayProgram=0,postProgram=0,blurProgram=0,meshProgram=0,lineProgram=0;
    GLuint meshVAO=0,meshVBO=0,lineVAO=0,lineVBO=0;int meshCount=0;
    Target scene,blur[2];std::vector<std::vector<V3>> paths;
    void lines(const std::vector<V3>&points,V3 color,const Mat4&mvp,bool strip=true,float alpha=1.f);
};
