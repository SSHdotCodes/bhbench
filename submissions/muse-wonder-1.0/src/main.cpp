// Black Hole — scientifically-grounded realtime simulation (C++ / OpenGL)
// Schwarzschild geodesic ray tracing + Novikov-Thorne accretion disk
// + Flamm-paraboloid spacetime embedding grid. Units: Rs = 1.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#endif
#include <GLFW/glfw3.h>

static std::string readFile(const std::string& path){
  std::ifstream f(path); if(!f) return "";
  std::stringstream ss; ss<<f.rdbuf(); return ss.str();
}
static std::string findShader(const std::string& name){
  const char* dirs[] = {"shaders/","../shaders/","../../shaders/",
    "black-hole-cpp-musewonder/shaders/","./black-hole-cpp-musewonder/shaders/"};
  for(auto d: dirs){ std::string p=std::string(d)+name; std::string s=readFile(p); if(!s.empty()) return s; }
  return "";
}
static GLuint compileShader(GLenum type, const std::string& src, const char* label){
  GLuint sh=glCreateShader(type); const char* c=src.c_str();
  glShaderSource(sh,1,&c,nullptr); glCompileShader(sh);
  GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[4096]; glGetShaderInfoLog(sh,sizeof(log),nullptr,log);
    fprintf(stderr,"[shader %s] %s\n",label,log); exit(1); }
  return sh;
}
static GLuint makeProgram(const std::string& vsName, const std::string& fsName){
  std::string vs=findShader(vsName), fs=findShader(fsName);
  if(vs.empty()||fs.empty()){ fprintf(stderr,"Cannot find shaders %s / %s\n",vsName.c_str(),fsName.c_str()); exit(1); }
  GLuint p=glCreateProgram();
  glAttachShader(p, compileShader(GL_VERTEX_SHADER,vs,vsName.c_str()));
  glAttachShader(p, compileShader(GL_FRAGMENT_SHADER,fs,fsName.c_str()));
  glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){ char log[4096]; glGetProgramInfoLog(p,sizeof(log),nullptr,log); fprintf(stderr,"[link] %s\n",log); exit(1); }
  return p;
}
// ---- minimal vec/mat math ----
struct Vec3{ float x,y,z; Vec3():x(0),y(0),z(0){} Vec3(float a,float b,float c):x(a),y(b),z(c){}
  Vec3 operator+(const Vec3&o)const{return{x+o.x,y+o.y,z+o.z};}
  Vec3 operator-(const Vec3&o)const{return{x-o.x,y-o.y,z-o.z};}
  Vec3 operator*(float s)const{return{x*s,y*s,z*s};}
};
static Vec3 cross(const Vec3&a,const Vec3&b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 norm(Vec3 a){ float l=sqrtf(dot(a,a)); return l>0?a*(1.0f/l):a; }
struct Mat4{ float m[16]; };
static Mat4 perspective(float fov,float asp,float n,float f){
  Mat4 r{}; float t=1.0f/tanf(fov*0.5f);
  r.m[0]=t/asp; r.m[5]=t; r.m[10]=(f+n)/(n-f); r.m[11]=-1; r.m[14]=(2*f*n)/(n-f); return r;
}
static Mat4 lookAt(Vec3 eye, Vec3 ctr, Vec3 up){
  Vec3 fwd=norm(ctr-eye); Vec3 r=norm(cross(fwd,up)); Vec3 u=cross(r,fwd);
  Mat4 m{}; m.m[0]=r.x; m.m[4]=r.y; m.m[8]=r.z;
  m.m[1]=u.x; m.m[5]=u.y; m.m[9]=u.z;
  m.m[2]=-fwd.x; m.m[6]=-fwd.y; m.m[10]=-fwd.z;
  m.m[12]=-dot(r,eye); m.m[13]=-dot(u,eye); m.m[14]=dot(fwd,eye); m.m[15]=1; return m;
}
static Mat4 mul(const Mat4&a,const Mat4&b){
  Mat4 r{}; for(int c=0;c<4;c++) for(int i=0;i<4;i++){ float s=0; for(int k=0;k<4;k++) s+=a.m[k*4+i]*b.m[c*4+k]; r.m[c*4+i]=s; } return r;
}
static Mat4 translate(float x,float y,float z){ Mat4 m{}; m.m[0]=m.m[5]=m.m[10]=m.m[15]=1; m.m[12]=x; m.m[13]=y; m.m[14]=z; return m; }

// App state
static double g_theta=0.7, g_phi=1.22, g_radius=11.5;
static bool g_drag=false; static double g_lastX=0,g_lastY=0;
static bool g_showDisk=true, g_showGrid=true, g_lensing=true, g_pause=false, g_autoRotate=true;
static int  g_steps=280, g_viewMode=3; // 1=hole 2=grid 3=both
static bool g_shot=false;
static float g_exposure=1.15f;

static void onMouse(GLFWwindow* w,int b,int a,int){ if(b==GLFW_MOUSE_BUTTON_LEFT){ g_drag=(a==GLFW_PRESS); glfwGetCursorPos(w,&g_lastX,&g_lastY);} }
static void onMove(GLFWwindow* w,double x,double y){
  if(g_drag){ g_theta+=(x-g_lastX)*0.005; g_phi-=(y-g_lastY)*0.005; if(g_phi<0.08)g_phi=0.08; if(g_phi>3.06)g_phi=3.06; }
  g_lastX=x; g_lastY=y;
}
static void onScroll(GLFWwindow*,double,double dy){ g_radius*= (dy>0?0.92f:1.09f); if(g_radius<4)g_radius=4; if(g_radius>35)g_radius=35; }
static void onKey(GLFWwindow* w,int key,int,int act,int){
  if(act!=GLFW_PRESS) return;
  if(key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w,1);
  if(key==GLFW_KEY_D) g_showDisk=!g_showDisk;
  if(key==GLFW_KEY_G) g_showGrid=!g_showGrid;
  if(key==GLFW_KEY_L) g_lensing=!g_lensing;
  if(key==GLFW_KEY_SPACE) g_pause=!g_pause;
  if(key==GLFW_KEY_R){ g_theta=0.7; g_phi=1.22; g_radius=11.5; }
  if(key==GLFW_KEY_P) g_shot=true;
  if(key==GLFW_KEY_1) g_viewMode=1;
  if(key==GLFW_KEY_2) g_viewMode=2;
  if(key==GLFW_KEY_3) g_viewMode=3;
  if(key==GLFW_KEY_Q){ g_steps = (g_steps==160?280:(g_steps==280?420:160)); printf("Steps -> %d\n",g_steps); }
  if(key==GLFW_KEY_EQUAL||key==GLFW_KEY_KP_ADD) g_exposure*=1.12f;
  if(key==GLFW_KEY_MINUS||key==GLFW_KEY_KP_SUBTRACT) g_exposure/=1.12f;
  printf("[keys] disk=%d grid=%d lensing=%d steps=%d view=%d exposure=%.2f\n",
         g_showDisk,g_showGrid,g_lensing,g_steps,g_viewMode,g_exposure);
}

// Build spacetime grid line geometry (polar web in XZ plane)
static void buildGrid(GLuint& vao, GLuint& vbo, GLsizei& count){
  std::vector<float> pts;
  auto seg=[&](float x1,float z1,float x2,float z2){ pts.push_back(x1);pts.push_back(0);pts.push_back(z1); pts.push_back(x2);pts.push_back(0);pts.push_back(z2); };
  // Concentric rings, denser near horizon
  const int NR=42;
  for(int i=0;i<NR;i++){
    float f=(float)i/(NR-1); float r=1.02f + (14.0f-1.02f)*f*f*0.35f + f*2.0f; if(r>14)r=14;
    const int S=128;
    for(int j=0;j<S;j++){ float a1=j/(float)S*6.2831853f, a2=(j+1)/(float)S*6.2831853f;
      seg(cosf(a1)*r,sinf(a1)*r,cosf(a2)*r,sinf(a2)*r); }
  }
  // Radial spokes
  const int NS=48;
  for(int s=0;s<NS;s++){ float a=s/(float)NS*6.2831853f;
    const int S=64; float prevR=1.02f;
    for(int j=1;j<=S;j++){ float f=j/(float)S; float r=1.02f+(14.0f-1.02f)*f;
      seg(cosf(a)*prevR,sinf(a)*prevR,cosf(a)*r,sinf(a)*r); prevR=r; } }
  count=(GLsizei)(pts.size()/3);
  glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
  glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER, pts.size()*sizeof(float), pts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
  glBindVertexArray(0);
}

int main(int argc, char** argv){
  (void)argc; (void)argv;
  if(!glfwInit()){ fprintf(stderr,"GLFW init failed\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
  glfwWindowHint(GLFW_SAMPLES,4);
  GLFWwindow* win=glfwCreateWindow(1280,800,"Black Hole — Schwarzschild Geodesic Ray Tracing (Rs=1)",nullptr,nullptr);
  if(!win){ fprintf(stderr,"Window creation failed\n"); glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win); glfwSwapInterval(1);
  glfwSetMouseButtonCallback(win,onMouse); glfwSetCursorPosCallback(win,onMove);
  glfwSetScrollCallback(win,onScroll); glfwSetKeyCallback(win,onKey);

  GLuint bhProg=makeProgram("blackhole.vert","blackhole.frag");
  GLuint gridProg=makeProgram("grid.vert","grid.frag");
  // Fullscreen quad
  float quad[]={-1,-1, 1,-1, -1,1, 1,1};
  GLuint quadVAO,quadVBO; glGenVertexArrays(1,&quadVAO); glGenBuffers(1,&quadVBO);
  glBindVertexArray(quadVAO); glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
  glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
  GLuint gridVAO,gridVBO; GLsizei gridCount=0; buildGrid(gridVAO,gridVBO,gridCount);

  printf("=== Black Hole Simulation (C++/OpenGL) ===\n");
  printf("Physics: Schwarzschild metric, Rs=1. Horizon r=1, photon sphere r=1.5, ISCO r=3.\n");
  printf("Ray tracing: null-geodesic integration  a = -3/2 h^2 x/|x|^5 (leapfrog, adaptive step).\n");
  printf("Disk: Novikov-Thorne T(r), Keplerian Doppler beaming + gravitational redshift (g^3).\n");
  printf("Grid: Flamm paraboloid z=2 sqrt(Rs(r-Rs)) embedding of the spatial slice.\n");
  printf("Controls: drag=orbit  scroll=zoom  D=disk G=grid L=lensing 1/2/3=view Q=quality +/-=exposure SPACE=pause R=reset ESC=quit\n");

  // One-frame renderer shared by interactive loop and capture/bench modes
  auto renderFrame = [&](int W,int H,float simT){
    if(H==0)H=1;
    glViewport(0,0,W,H); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float sp=(float)sin(g_phi), cp=(float)cos(g_phi);
    Vec3 cam{ (float)(g_radius*sp*sin(g_theta)), (float)(g_radius*cp), (float)(g_radius*sp*cos(g_theta)) };
    Vec3 fwd=norm(Vec3{0,0,0}-cam);
    Vec3 right=norm(cross(fwd,Vec3{0,1,0})); Vec3 up=cross(right,fwd);
    if(g_viewMode==1 || g_viewMode==3){
      glUseProgram(bhProg);
      glUniform2f(glGetUniformLocation(bhProg,"uRes"),(float)W,(float)H);
      glUniform1f(glGetUniformLocation(bhProg,"uTime"),simT);
      glUniform3f(glGetUniformLocation(bhProg,"uCamPos"),cam.x,cam.y,cam.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamRight"),right.x,right.y,right.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamUp"),up.x,up.y,up.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamFwd"),fwd.x,fwd.y,fwd.z);
      glUniform1i(glGetUniformLocation(bhProg,"uSteps"),g_steps);
      glUniform1f(glGetUniformLocation(bhProg,"uShowDisk"),g_showDisk?1.0f:0.0f);
      glUniform1f(glGetUniformLocation(bhProg,"uLensing"),g_lensing?1.0f:0.0f);
      glUniform1f(glGetUniformLocation(bhProg,"uExposure"),g_exposure);
      glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    } else {
      glClearColor(0.004f,0.005f,0.012f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    }
    if((g_viewMode==2 || g_viewMode==3) && g_showGrid){
      Mat4 proj=perspective(0.95f,(float)W/H,0.1f,200.0f);
      Mat4 view=lookAt(cam,Vec3{0,-0.4f,0},Vec3{0,1,0});
      Mat4 model=translate(0.0f,-1.55f,0.0f);
      Mat4 mvp=mul(proj,mul(view,model));
      glUseProgram(gridProg);
      glUniformMatrix4fv(glGetUniformLocation(gridProg,"uMVP"),1,GL_FALSE,mvp.m);
      glUniform1f(glGetUniformLocation(gridProg,"uTime"),simT);
      glUniform3f(glGetUniformLocation(gridProg,"uColor"),0.25f,0.55f,0.95f);
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_LINE_SMOOTH); glLineWidth(1.0f);
      glBindVertexArray(gridVAO); glDrawArrays(GL_LINES,0,gridCount);
      glDisable(GL_BLEND);
    }
  };

  // --capture out.ppm theta phi radius disk grid lens view steps time W H
  if(argc>2 && strcmp(argv[1],"--capture")==0){
    auto A=[&](int i,double d){ return i<argc?atof(argv[i]):d; };
    auto AI=[&](int i,int d){ return i<argc?atoi(argv[i]):d; };
    g_theta=A(3,g_theta); g_phi=A(4,g_phi); g_radius=A(5,g_radius);
    g_showDisk=AI(6,1)!=0; g_showGrid=AI(7,1)!=0; g_lensing=AI(8,1)!=0;
    g_viewMode=AI(9,3); g_steps=AI(10,280); float simT=(float)A(11,2.0);
    int W=AI(12,960), H=AI(13,600);
    glfwShowWindow(win);
    for(int i=0;i<3;i++){ glfwPollEvents(); renderFrame(W,H,simT); glfwSwapBuffers(win); glFinish(); }
    // Re-render and read the back buffer before presenting it
    glfwPollEvents(); renderFrame(W,H,simT); glFinish();
    std::vector<unsigned char> px((size_t)W*H*3);
    glReadBuffer(GL_BACK);
    glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px.data());
    FILE* f=fopen(argv[2],"wb"); if(!f){ fprintf(stderr,"cannot write %s\n",argv[2]); return 1; }
    fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(int y=H-1;y>=0;y--) fwrite(&px[(size_t)y*W*3],1,(size_t)W*3,f);
    fclose(f); printf("CAPTURED %s %dx%d theta=%.2f phi=%.2f r=%.1f disk=%d grid=%d lens=%d view=%d steps=%d t=%.1f\n",
      argv[2],W,H,g_theta,g_phi,g_radius,g_showDisk,g_showGrid,g_lensing,g_viewMode,g_steps,simT);
    glfwTerminate(); return 0;
  }
  // --bench frames W H : render N frames, report ms/frame and equiv FPS
  if(argc>1 && strcmp(argv[1],"--bench")==0){
    int n = argc>2?atoi(argv[2]):120; int W = argc>3?atoi(argv[3]):1280; int H = argc>4?atoi(argv[4]):800;
    glfwShowWindow(win); glfwPollEvents();
    renderFrame(W,H,2.0f); glfwSwapBuffers(win); glFinish();
    double b0=glfwGetTime();
    for(int i=0;i<n;i++){ glfwPollEvents(); renderFrame(W,H,2.0f+(float)i*0.016f); glfwSwapBuffers(win); }
    glFinish(); double el=glfwGetTime()-b0;
    printf("BENCH frames=%d res=%dx%d steps=%d ms_per_frame=%.2f fps=%.1f\n",n,W,H,g_steps,el*1000.0/n,n/el);
    glfwTerminate(); return 0;
  }

  double t0=glfwGetTime(), lastTitle=t0; int frames=0; float simTime=0.0f;
  while(!glfwWindowShouldClose(win)){
    double now=glfwGetTime(); double dt=now-t0; t0=now; frames++;
    if(!g_pause) simTime += (float)dt;
    if(g_autoRotate && !g_drag && !g_pause) g_theta += dt*0.045;
    if(now-lastTitle>0.5){ char title[256];
      snprintf(title,sizeof(title),"Black Hole — %.0f FPS | steps=%d | disk:%s grid:%s lensing:%s | drag orbit · scroll zoom · D/G/L/1/2/3/Q",
        frames/(now-lastTitle), g_steps, g_showDisk?"ON":"OFF", g_showGrid?"ON":"OFF", g_lensing?"ON":"OFF");
      glfwSetWindowTitle(win,title); frames=0; lastTitle=now; }
    int W,H; glfwGetFramebufferSize(win,&W,&H); if(H==0)H=1;
    glViewport(0,0,W,H); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    float sp=(float)sin(g_phi), cp=(float)cos(g_phi);
    Vec3 cam{ (float)(g_radius*sp*sin(g_theta)), (float)(g_radius*cp), (float)(g_radius*sp*cos(g_theta)) };
    Vec3 fwd=norm(Vec3{0,0,0}-cam);
    Vec3 right=norm(cross(fwd,Vec3{0,1,0})); Vec3 up=cross(right,fwd);

    if(g_viewMode==1 || g_viewMode==3){
      glUseProgram(bhProg);
      glUniform2f(glGetUniformLocation(bhProg,"uRes"),(float)W,(float)H);
      glUniform1f(glGetUniformLocation(bhProg,"uTime"),simTime);
      glUniform3f(glGetUniformLocation(bhProg,"uCamPos"),cam.x,cam.y,cam.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamRight"),right.x,right.y,right.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamUp"),up.x,up.y,up.z);
      glUniform3f(glGetUniformLocation(bhProg,"uCamFwd"),fwd.x,fwd.y,fwd.z);
      glUniform1i(glGetUniformLocation(bhProg,"uSteps"),g_steps);
      glUniform1f(glGetUniformLocation(bhProg,"uShowDisk"),g_showDisk?1.0f:0.0f);
      glUniform1f(glGetUniformLocation(bhProg,"uLensing"),g_lensing?1.0f:0.0f);
      glUniform1f(glGetUniformLocation(bhProg,"uExposure"),g_exposure);
      glBindVertexArray(quadVAO); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    } else {
      glClearColor(0.004f,0.005f,0.012f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    }
    if((g_viewMode==2 || g_viewMode==3) && g_showGrid){
      // Grid sits under the hole: same camera, funnel throat at origin dropped down
      Mat4 proj=perspective(0.95f,(float)W/H,0.1f,200.0f);
      Mat4 view=lookAt(cam,Vec3{0,-0.4f,0},Vec3{0,1,0});
      Mat4 model=translate(0.0f,-1.55f,0.0f);
      Mat4 mvp=mul(proj,mul(view,model));
      glUseProgram(gridProg);
      glUniformMatrix4fv(glGetUniformLocation(gridProg,"uMVP"),1,GL_FALSE,mvp.m);
      glUniform1f(glGetUniformLocation(gridProg,"uTime"),simTime);
      glUniform3f(glGetUniformLocation(gridProg,"uColor"),0.25f,0.55f,0.95f);
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_LINE_SMOOTH); glLineWidth(1.0f);
      glBindVertexArray(gridVAO); glDrawArrays(GL_LINES,0,gridCount);
      glDisable(GL_BLEND);
    }
    if(g_shot){ // P: save the actual live frame
      g_shot=false; glfwGetFramebufferSize(win,&W,&H);
      std::vector<unsigned char> px((size_t)W*H*3);
      glReadBuffer(GL_BACK); glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px.data());
      FILE* f=fopen("/tmp/bh_live.ppm","wb");
      if(f){ fprintf(f,"P6\n%d %d\n255\n",W,H); for(int y=H-1;y>=0;y--) fwrite(&px[(size_t)y*W*3],1,(size_t)W*3,f); fclose(f);
        printf("[live] frame saved /tmp/bh_live.ppm disk=%d grid=%d lens=%d view=%d steps=%d\n",g_showDisk,g_showGrid,g_lensing,g_viewMode,g_steps); fflush(stdout); }
    }
    glfwSwapBuffers(win); glfwPollEvents();
  }
  glfwTerminate(); return 0;
}

