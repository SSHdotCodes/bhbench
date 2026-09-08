#include "graphics.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.hpp"
#include "physics.hpp"
#include <png.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs=std::filesystem;
struct App {
    SceneSettings settings;
    int view=0,quality=1;
    bool paused=false,autoOrbit=false,info=false,hideUI=false,screenshot=false,rays=true,dragging=false,pressed=false,pressPending=false;
    float speed=1,meshAz=.65f,meshEl=.70f,meshDistance=84,fps=0;
    double lastX=0,lastY=0;
};
static std::string number(float v,int digits=1){std::ostringstream s;s<<std::fixed<<std::setprecision(digits)<<v;return s.str();}
static void reset(App&a){a.settings=SceneSettings{};a.meshAz=.65f;a.meshEl=.70f;a.meshDistance=84;a.autoOrbit=false;a.speed=1;}
static void key(GLFWwindow*w,int key,int,int action,int){
    if(action!=GLFW_PRESS)return;auto&a=*static_cast<App*>(glfwGetWindowUserPointer(w));
    switch(key){
        case GLFW_KEY_ESCAPE:if(a.info)a.info=false;else glfwSetWindowShouldClose(w,1);break;
        case GLFW_KEY_1:a.view=0;break;case GLFW_KEY_2:a.view=1;break;
        case GLFW_KEY_SPACE:a.paused=!a.paused;break;case GLFW_KEY_R:reset(a);break;
        case GLFW_KEY_G:a.settings.skyGrid=!a.settings.skyGrid;break;
        case GLFW_KEY_L:a.settings.lensing=!a.settings.lensing;break;
        case GLFW_KEY_D:a.settings.disk=!a.settings.disk;break;
        case GLFW_KEY_B:a.settings.bloom=!a.settings.bloom;break;
        case GLFW_KEY_C:a.settings.colorMode=(a.settings.colorMode+1)%3;break;
        case GLFW_KEY_T:a.settings.texture=!a.settings.texture;break;
        case GLFW_KEY_S:a.settings.stars=!a.settings.stars;break;
        case GLFW_KEY_O:a.autoOrbit=!a.autoOrbit;break;
        case GLFW_KEY_H:a.info=!a.info;break;
        case GLFW_KEY_TAB:a.hideUI=!a.hideUI;break;
        case GLFW_KEY_Q:a.quality=(a.quality+1)%3;break;
        case GLFW_KEY_F12:a.screenshot=true;break;
    }
}
static void scroll(GLFWwindow*w,double,double y){auto&a=*static_cast<App*>(glfwGetWindowUserPointer(w));if(a.info)return;if(a.view==0)a.settings.distance=std::clamp(a.settings.distance*float(std::exp(-y*.055)),30.f,110.f);else a.meshDistance=std::clamp(a.meshDistance*float(std::exp(-y*.055)),45.f,150.f);}
static void mouseButton(GLFWwindow*w,int button,int action,int){
    if(button!=GLFW_MOUSE_BUTTON_LEFT)return;auto&a=*static_cast<App*>(glfwGetWindowUserPointer(w));
    if(action==GLFW_PRESS){
        a.pressPending=true;double x,y;glfwGetCursorPos(w,&x,&y);int width,height;glfwGetWindowSize(w,&width,&height);
        a.lastX=x;a.lastY=y;a.dragging=!a.info&&(a.hideUI||(x>316&&y>226&&y<height-240));
        if(a.dragging)a.autoOrbit=false;
    }else if(action==GLFW_RELEASE)a.dragging=false;
}
static void cursor(GLFWwindow*w,double x,double y){
    auto&a=*static_cast<App*>(glfwGetWindowUserPointer(w));
    if(a.dragging&&!a.info){float dx=float(x-a.lastX),dy=float(y-a.lastY);
        if(a.view==0){a.settings.azimuth-=dx*.005f;a.settings.inclination=std::clamp(a.settings.inclination+dy*.16f,2.f,89.5f);}
        else{a.meshAz-=dx*.005f;a.meshEl=std::clamp(a.meshEl+dy*.004f,.13f,1.3f);}
    }
    a.lastX=x;a.lastY=y;
}
static bool capturePNG(const fs::path&path,int w,int h){
    fs::create_directories(path.parent_path());std::vector<unsigned char>pixels(size_t(w)*h*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    FILE*f=std::fopen(path.string().c_str(),"wb");if(!f)return false;
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);png_infop info=png_create_info_struct(png);
    if(setjmp(png_jmpbuf(png))){png_destroy_write_struct(&png,&info);std::fclose(f);return false;}
    png_init_io(png,f);png_set_IHDR(png,info,w,h,8,PNG_COLOR_TYPE_RGB,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);png_write_info(png,info);
    std::vector<png_bytep>rows(h);for(int i=0;i<h;i++)rows[i]=pixels.data()+size_t(h-1-i)*w*3;png_write_image(png,rows.data());png_write_end(png,nullptr);png_destroy_write_struct(&png,&info);std::fclose(f);return true;
}
static void overlay(UI&ui,App&a,int w,int h,int rw,int rh,bool valid){
    auto&s=a.settings;float W=float(w),H=float(h);Color rule{.11f,.15f,.17f,1};
    ui.rect(0,0,288,H,{.028f,.041f,.051f,1});ui.rect(288,0,1,H,rule);
    ui.text(28,25,"A S T R A   /   R E L A T I V I T Y",11,gold);
    ui.text(28,57,"Black hole",30);ui.text(29,98,"SCHWARZSCHILD  /  a* = 0",11,muted);ui.line(28,129,230,rule);
    ui.text(28,150,"OBSERVER",10,gold);ui.text(28,177,"Inclination",13);ui.text(208,177,number(s.inclination,0)+" deg",12,gold);ui.slider(1,29,209,229,s.inclination,2.f,89.5f);
    ui.text(28,231,"Distance",13);ui.text(205,231,number(s.distance,0)+" M",12,gold);ui.slider(2,29,263,229,s.distance,30.f,110.f);
    ui.text(28,286,"Exposure",13);ui.text(215,286,number(s.exposure)+"x",12,gold);ui.slider(3,29,318,229,s.exposure,.15f,4.f);
    ui.line(28,345,230,rule);ui.text(28,365,"LIGHT & MATTER",10,gold);
    ui.toggle(28,392,230,"GR ray bending",s.lensing);ui.toggle(28,429,230,"Relativistic frequency shift",s.redshift);
    ui.toggle(28,466,230,"Accretion disk",s.disk);ui.toggle(28,503,230,"Emission halo (model)",s.halo);
    ui.toggle(28,540,230,"Optical bloom",s.bloom);ui.toggle(28,577,230,"Celestial grid",s.skyGrid);
    ui.line(28,624,230,rule);ui.text(28,643,"DISPLAY",10,gold);
    if(ui.button(28,666,72,32,"THERMAL",s.colorMode==0))s.colorMode=0;
    if(ui.button(105,666,72,32,"SPECTRAL",s.colorMode==1))s.colorMode=1;
    if(ui.button(182,666,76,32,"SHIFT",s.colorMode==2))s.colorMode=2;
    ui.text(28,711,s.colorMode==0?"Bolometric intensity / false color":s.colorMode==1?"Planck spectrum / three RGB bands":"Frequency ratio g / diagnostic",10,muted);
    ui.text(28,750,"RENDER QUALITY",10,gold);
    if(ui.button(28,773,72,30,"FAST",a.quality==0))a.quality=0;
    if(ui.button(105,773,72,30,"BALANCED",a.quality==1))a.quality=1;
    if(ui.button(182,773,76,30,"FINE",a.quality==2))a.quality=2;
    ui.text(28,H-54,"OpenGL 4.1 / GPU null geodesics",10,muted);
    ui.text(28,H-34,valid?"PHYSICS CHECKS PASSED":"PHYSICS CHECKS FAILED",10,valid?cyan:gold);

    float X=316;
    ui.text(X,24,"GENERAL RELATIVITY   /   INTERACTIVE OBSERVATORY",10,muted);
    ui.text(X,48,a.view==0?"The shape of gravity.":"A trapdoor in spacetime.",30);
    if(ui.button(W-234,29,94,34,"SNAPSHOT"))a.screenshot=true;
    if(ui.button(W-126,29,98,34,"PHYSICS  H",a.info))a.info=!a.info;
    if(ui.button(X,108,168,35,"01   OBSERVER",a.view==0))a.view=0;
    if(ui.button(X+175,108,216,35,"02   SPATIAL CURVATURE",a.view==1))a.view=1;
    ui.text(W-193,115,number(a.fps,0)+" FPS  /  "+(a.view==0?std::to_string(rw)+" x "+std::to_string(rh):"SPATIAL SLICE"),11,cyan);

    if(a.view==0){
        ui.text(X+20,173,"NULL GEODESIC RAY TRACING",10,{.56f,.61f,.63f,.8f});
        ui.text(X+20,193,"Light bent around a nonrotating black hole",12,{.60f,.65f,.67f,.8f});
        ui.text(X+20,H-195,"6M - 28M  /  RELATIVISTIC THIN DISK",10,muted);
        ui.text(X+20,H-176,"Drag to orbit  /  scroll to move",11,muted);
        ui.text(W-284,H-368,"CURVED SPATIAL SLICE",10,cyan);
        ui.text(W-284,H-347,"Flamm embedding  /  click to explore",10,muted);
        if(ui.hover(W-298,H-329,272,175)&&a.pressed)a.view=1;
    }else{
        ui.text(X+22,176,"EQUATORIAL SLICE  /  CONSTANT SCHWARZSCHILD TIME",10,cyan);
        ui.text(X+22,197,"ds^2 = dr^2 / (1 - 2M/r) + r^2 dphi^2",14,ink);
        ui.text(X+22,H-228,"EMBEDDING HEIGHT  z = 2 sqrt[ 2M (r - 2M) ]",11,cyan);
        ui.text(X+22,H-205,"The height is a diagram dimension. The surface ends at r = 2M.",12,ink);
        ui.text(X+22,H-184,"Mapped photon paths are spacetime geodesics, not shortest paths on this surface.",11,muted);
        if(ui.button(W-203,173,153,31,a.rays?"PHOTON PATHS  ON":"PHOTON PATHS  OFF",a.rays))a.rays=!a.rays;
    }
    float cardY=H-136,cardW=(W-X-32)/3;
    const char*labels[]={"EVENT HORIZON","PHOTON SPHERE","INNER STABLE ORBIT"};const char*values[]={"2.000 M","3.000 M","6.000 M"};
    const char*notes[]={"No future-directed light escapes","Unstable circular light orbits","Inner edge of the thin disk"};
    for(int i=0;i<3;i++){float cx=X+i*cardW;ui.line(cx,cardY,cardW-17,rule);ui.text(cx,cardY+14,labels[i],10,muted);ui.text(cx,cardY+34,values[i],23,i==0?gold:(i==1?cyan:ink));ui.text(cx,cardY+67,notes[i],11,muted);}
    ui.line(X,H-41,W-X-27,rule);
    if(ui.button(X,H-31,84,24,a.paused?"PLAY":"PAUSE",a.paused))a.paused=!a.paused;
    if(ui.button(X+93,H-31,103,24,"AUTO ORBIT",a.autoOrbit))a.autoOrbit=!a.autoOrbit;
    if(ui.button(X+205,H-31,70,24,"RESET"))reset(a);
    ui.text(X+299,H-27,"TIME "+number(a.speed)+"x",10,muted);ui.slider(4,X+381,H-19,90,a.speed,.1f,4.f);
    ui.text(W-215,H-27,"G = c = M = 1   /   TAB: CLEAN",10,muted);
}
static void infoOverlay(UI&ui,App&a,int w,int h){
    ui.rect(0,0,float(w),float(h),{.005f,.01f,.015f,.86f});float x=(w-800.f)/2,y=(h-646.f)/2;
    ui.rect(x,y,800,646,{.035f,.055f,.069f,1});ui.rect(x,y,3,646,gold);
    ui.text(x+32,y+27,"WHAT IS PHYSICAL",11,gold);ui.text(x+32,y+54,"Exact spacetime. Explicit approximations.",26);
    std::vector<std::pair<std::string,std::string>>lines={
        {"SPACETIME","Schwarzschild: a spherical, nonrotating, uncharged black hole."},
        {"LIGHT PATHS","Fourth-order Runge-Kutta: u'' + u = 3u^2, with u = M/r."},
        {"CAMERA","Static local orthonormal frame; finite observer radius is included."},
        {"DISK","Page-Thorne radial flux; circular Keplerian motion outside r = 6M."},
        {"LIGHT TRANSFER","Gravitational redshift + Doppler shift; bolometric intensity scales as g^4."},
        {"LENSED ARCS","Direct and higher-order disk images come from the integrated light paths."},
        {"SPATIAL GRID","Flamm's paraboloid: an exact embedding of an exterior spatial slice."},
        {"DISPLAY","Thermal palette is false color. Bloom is an optical display effect."},
        {"APPROXIMATIONS","Thin opaque disk, procedural texture, prescribed optically thin corona."},
        {"LIMITS","No spin, magnetic fluid evolution, disk self-gravity, or wave optics."},
        {"TIME","1 real second = 20 GM/c^3 at 1x; the disk uses retarded emission time."}
    };
    float yy=y+113;for(auto&l:lines){ui.text(x+32,yy,l.first,10,cyan);ui.text(x+32,yy+18,l.second,12,ink);yy+=42;}
    ui.text(x+32,y+590,"1 / 2  Views    SPACE  Pause    G  Sky grid    C  Color    Q  Quality    F12  Snapshot",11,muted);
    if(ui.button(x+660,y+26,108,31,"CLOSE  H"))a.info=false;
}
int main(int argc,char**argv){
    int frameLimit=0,initialView=0,quality=1,width=1440,height=960;bool gpuOnly=false,hidden=false,clean=false;std::string capturePath;
    for(int i=1;i<argc;i++){
        std::string arg=argv[i];
        if(arg=="--frames"&&i+1<argc)frameLimit=std::stoi(argv[++i]);
        else if(arg=="--capture"&&i+1<argc)capturePath=argv[++i];
        else if(arg=="--view"&&i+1<argc)initialView=std::string(argv[++i])=="curvature"?1:0;
        else if(arg=="--quality"&&i+1<argc)quality=std::clamp(std::stoi(argv[++i]),0,2);
        else if(arg=="--size"&&i+2<argc){width=std::stoi(argv[++i]);height=std::stoi(argv[++i]);}
        else if(arg=="--validate-gpu"){gpuOnly=true;hidden=true;}
        else if(arg=="--hidden")hidden=true;
        else if(arg=="--clean")clean=true;
        else if(arg=="--help"){std::cout<<"Black Hole / Astra 6\n--frames N --capture file.png --view observer|curvature --quality 0|1|2\n--size W H --clean --hidden --validate-gpu\n";return 0;}
        else{std::cerr<<"Unknown option: "<<arg<<"\n";return 1;}
    }
    glfwSetErrorCallback([](int code,const char*text){std::cerr<<"GLFW "<<code<<": "<<text<<'\n';});
    if(!glfwInit())return 1;glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);glfwWindowHint(GLFW_DEPTH_BITS,24);glfwWindowHint(GLFW_VISIBLE,hidden?GLFW_FALSE:GLFW_TRUE);
    GLFWwindow*window=glfwCreateWindow(width,height,"Astra / Black Hole - Schwarzschild Observatory",nullptr,nullptr);if(!window){glfwTerminate();return 1;}
    glfwSetWindowSizeLimits(window,1280,920,GLFW_DONT_CARE,GLFW_DONT_CARE);
    if(!hidden){int mx,my,mw,mh;glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(),&mx,&my,&mw,&mh);glfwSetWindowPos(window,mx+std::max(0,(mw-width)/2),my+std::max(30,(mh-height)/2));}
    glfwMakeContextCurrent(window);glfwSwapInterval(1);
    int result=0;
    try{
        Renderer renderer;renderer.init();std::string report;bool valid=renderer.validateGPU(report);
        std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<'\n'<<report<<std::flush;
        fs::create_directories(fs::path(BH_ROOT)/"work");std::ofstream(fs::path(BH_ROOT)/"work/gpu-validation.txt")<<report;
        if(gpuOnly){result=valid?0:2;}
        else{
            App app;app.view=initialView;app.quality=quality;app.hideUI=clean;glfwSetWindowUserPointer(window,&app);glfwSetKeyCallback(window,key);glfwSetScrollCallback(window,scroll);glfwSetMouseButtonCallback(window,mouseButton);glfwSetCursorPosCallback(window,cursor);UI ui;ui.init();
            int frames=0;double previous=glfwGetTime(),fpsStart=previous,benchStart=0;int fpsFrames=0;std::string savedNotice;double savedUntil=0;
            while(!glfwWindowShouldClose(window)){
                glfwPollEvents();int w,h,fw,fh;glfwGetWindowSize(window,&w,&h);glfwGetFramebufferSize(window,&fw,&fh);
                if(fw<=0||fh<=0){glfwWaitEventsTimeout(.05);continue;}
                double now=glfwGetTime(),dt=std::min(now-previous,.1);previous=now;
                if(frames==10)benchStart=now;
                if(!app.paused)app.settings.time+=float(dt)*20.f*app.speed;
                if(app.autoOrbit&&!app.paused){if(app.view==0)app.settings.azimuth+=float(dt)*.06f;else app.meshAz+=float(dt)*.09f;}
                double mx,my;glfwGetCursorPos(window,&mx,&my);bool down=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;app.pressed=app.pressPending;app.pressPending=false;
                float scaleX=float(fw)/w,scaleY=float(fh)/h;
                int vx=app.hideUI?0:306,vy=app.hideUI?0:154,vw=app.hideUI?w:w-324,vh=app.hideUI?h:h-308;
                int targetHeight=app.quality==0?440:(app.quality==1?640:1000);
                int rh=std::min(int(vh*scaleY),targetHeight),rw=int(float(vw)/vh*rh);rw=std::max(rw,8);rh=std::max(rh,8);
                app.settings.step=app.quality==0?.045f:(app.quality==1?.025f:.012f);
                glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,fw,fh);glClearColor(.014f,.022f,.029f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                if(app.view==0){
                    renderer.render(app.settings,rw,rh);renderer.present(int(vx*scaleX),int((h-vy-vh)*scaleY),int(vw*scaleX),int(vh*scaleY),app.settings);
                    if(!app.hideUI)renderer.embedding(int((w-298)*scaleX),int(154*scaleY),int(272*scaleX),int(175*scaleY),.65f,.48f,100.f,app.settings.time*.05f,false);
                }else renderer.embedding(int(vx*scaleX),int((h-vy-vh)*scaleY),int(vw*scaleX),int(vh*scaleY),app.meshAz,app.meshEl,app.meshDistance,app.settings.time*.05f,app.rays);
                fpsFrames++;if(now-fpsStart>.12){app.fps=float(fpsFrames/(now-fpsStart));fpsFrames=0;fpsStart=now;}
                ui.begin(float(w),float(h),app.info?-1000.f:float(mx),float(my),app.pressed&&!app.info,down&&!app.info);
                if(!app.hideUI)overlay(ui,app,w,h,rw,rh,valid);
                if(app.info){ui.input(float(mx),float(my),app.pressed,down);infoOverlay(ui,app,w,h);}
                if(savedUntil>now){ui.rect(w/2.f-185,h-100,370,36,{.10f,.14f,.16f,.96f});ui.text(w/2.f-170,h-91,savedNotice,12,gold);}
                ui.draw(fw,fh);
                if(app.screenshot||(!capturePath.empty()&&frames==(frameLimit>0?frameLimit-1:60))){
                    fs::path path=capturePath.empty()?fs::path(BH_ROOT)/"captures"/("snapshot-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".png"):fs::absolute(capturePath);
                    bool success=capturePNG(path,fw,fh);std::cout<<(success?"Saved ":"Failed to save ")<<path<<std::endl;app.screenshot=false;capturePath.clear();savedNotice=success?"Snapshot saved in captures/":"Could not save snapshot";savedUntil=now+3.;
                }
                glfwSwapBuffers(window);frames++;
                if(frameLimit>0&&frames>=frameLimit){double elapsed=glfwGetTime()-benchStart;std::cout<<"BENCHMARK "<<frames-10<<" frames / "<<elapsed<<" sec = "<<(frames-10)/elapsed<<" FPS, "<<rw<<"x"<<rh<<" rays, quality="<<app.quality<<", view="<<app.view<<std::endl;break;}
            }
        }
    }catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<std::endl;result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
