#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int mode=0, disk=1; static bool paused=false, dragging=false, dirty=true; static float yaw=0.0f, inclination=0.28f, zoom=1.0f; static double lastX,lastY, simTime=0; static float fps=0; static GLFWwindow* window;
static std::string readFile(const std::string& p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot read "+p);std::stringstream s;s<<f.rdbuf();return s.str();}
static GLuint shader(GLenum type,const std::string& source){GLuint s=glCreateShader(type);const char* p=source.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){GLchar log[8192];glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log);}return s;}
static GLuint program(const std::string& vs,const std::string& fs){GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GLchar log[8192];glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log);}glDeleteShader(v);glDeleteShader(f);return p;}
static void key(GLFWwindow* w,int k,int,int action,int){if(action!=GLFW_PRESS)return;if(k==GLFW_KEY_ESCAPE)glfwSetWindowShouldClose(w,1);else if(k==GLFW_KEY_1)mode=0;else if(k==GLFW_KEY_2)mode=1;else if(k==GLFW_KEY_D)disk=!disk;else if(k==GLFW_KEY_SPACE)paused=!paused;else if(k==GLFW_KEY_R){yaw=0;inclination=0.28;zoom=1.0;}dirty=true;}
static void mouse(GLFWwindow* w,int button,int action,int){if(button==GLFW_MOUSE_BUTTON_LEFT){dragging=action==GLFW_PRESS;glfwGetCursorPos(w,&lastX,&lastY);}}
static void cursor(GLFWwindow*,double x,double y){if(dragging){yaw+=(x-lastX)*0.006;inclination=std::clamp(inclination+(y-lastY)*0.004,-1.35,1.35);lastX=x;lastY=y;}}
static void scroll(GLFWwindow*,double,double y){zoom=std::clamp(zoom*(float)std::exp(-y*0.12),0.35f,1.8f);}
static void textAt(NSString* s, CGFloat x,CGFloat y, CGFloat size, NSColor* color, NSFontWeight weight=NSFontWeightRegular){NSDictionary* a=@{NSFontAttributeName:[NSFont systemFontOfSize:size weight:weight],NSForegroundColorAttributeName:color};[s drawAtPoint:NSMakePoint(x,y) withAttributes:a];}
static GLuint makeHUD(int w,int h){
 @autoreleasepool {
  std::vector<unsigned char> pixels((size_t)w*h*4);CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();CGContextRef ctx=CGBitmapContextCreate(pixels.data(),w,h,8,w*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(cs);
  [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO]];
  CGFloat scale=std::max(1.0,std::min(w/1150.0,h/720.0));CGContextScaleCTM(ctx,scale,scale);CGFloat W=w/scale,H=h/scale;
  NSColor* white=[NSColor colorWithRed:0.91 green:0.95 blue:0.97 alpha:1];NSColor* muted=[NSColor colorWithRed:0.50 green:0.60 blue:0.65 alpha:1];NSColor* cyan=[NSColor colorWithRed:0.41 green:0.85 blue:0.83 alpha:1];NSColor* orange=[NSColor colorWithRed:1.0 green:0.58 blue:0.29 alpha:1];
  [[NSColor colorWithRed:0.025 green:0.045 blue:0.060 alpha:0.83] setFill];[[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(25,H-252,292,224) xRadius:12 yRadius:12] fill];
  [[NSColor colorWithRed:0.27 green:0.44 blue:0.48 alpha:0.42] setStroke];NSBezierPath* border=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(25,H-252,292,224) xRadius:12 yRadius:12];[border setLineWidth:0.7];[border stroke];
  textAt(@"FIELD NOTES  /  01",43,H-58,10,cyan,NSFontWeightBold);
  textAt(@"BLACK HOLE",43,H-91,25,white,NSFontWeightBold);
  textAt(@"Schwarzschild spacetime",44,H-111,12,muted);
  [[NSColor colorWithRed:0.35 green:0.48 blue:0.50 alpha:0.35] setFill];NSRectFill(NSMakeRect(43,H-129,255,1));
  textAt(mode==0?@"01  RAY-TRACED VIEW":@"02  SPATIAL CURVATURE",43,H-153,11,mode==0?orange:cyan,NSFontWeightBold);
  textAt(mode==0?@"Light follows null geodesics":@"Equatorial spatial embedding",43,H-173,12,white);
  textAt(mode==0?@"Thin disk  /  r = 6–22 GM/c²":@"Flamm paraboloid  /  t = constant",43,H-192,11,muted);
  textAt(mode==0?(disk?@"DISK ON":@"DISK OFF"):@"HORIZON  2M    ·    PHOTON SPHERE  3M",43,H-222,10,mode==0?orange:cyan,NSFontWeightSemibold);
  [[NSColor colorWithRed:0.025 green:0.045 blue:0.060 alpha:0.84] setFill];[[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(25,25,std::min(W-50,680.0),50) xRadius:9 yRadius:9] fill];
  textAt(@"1  LENSING     2  CURVATURE     D  DISK     SPACE  PAUSE     R  RESET",42,54,10,white,NSFontWeightSemibold);
  textAt(@"DRAG TO ORBIT    ·    SCROLL TO ZOOM    ·    ESC TO EXIT",42,36,10,muted);
  NSString* status=[NSString stringWithFormat:@"%@  ·  %.0f FPS",paused?@"PAUSED":@"LIVE",fps];textAt(status,W-122,H-48,10,cyan,NSFontWeightBold);
  [NSGraphicsContext restoreGraphicsState];CGContextRelease(ctx);
  GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return tex;
 }
}
int main(int argc,char** argv){try{
 bool benchmark=argc>1&&std::string(argv[1])=="--benchmark"; if(argc>1&&std::string(argv[1])=="--geometry")mode=1;
 if(!glfwInit())throw std::runtime_error("GLFW initialization failed");glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
 window=glfwCreateWindow(1200,760,"Black Hole | Schwarzschild Observatory",nullptr,nullptr);if(!window)throw std::runtime_error("OpenGL 4.1 window creation failed");glfwMakeContextCurrent(window);glfwSwapInterval(benchmark?0:1);glfwSetKeyCallback(window,key);glfwSetMouseButtonCallback(window,mouse);glfwSetCursorPosCallback(window,cursor);glfwSetScrollCallback(window,scroll);
 std::string root=SHADER_DIR;GLuint scene=program(readFile(root+"/scene.vert"),readFile(root+"/scene.frag"));
 const std::string vertex="#version 410 core\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
 const std::string fragment="#version 410 core\nin vec2 uv;out vec4 color;uniform sampler2D image;uniform int flip;void main(){color=texture(image,vec2(uv.x,flip==1?1.0-uv.y:uv.y));}";GLuint blit=program(vertex,fragment),vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 GLuint fbo,renderTex,hud=0;glGenFramebuffers(1,&fbo);glGenTextures(1,&renderTex);glBindTexture(GL_TEXTURE_2D,renderTex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 int oldW=0,oldH=0,frames=0,totalFrames=0;double previous=glfwGetTime(),timer=previous,start=previous;
 while(!glfwWindowShouldClose(window)){
  double now=glfwGetTime(),dt=std::min(now-previous,0.1);previous=now;if(!paused)simTime+=dt;int w,h;glfwGetFramebufferSize(window,&w,&h);if(w<1||h<1){glfwWaitEvents();continue;}
  if(w!=oldW||h!=oldH){oldW=w;oldH=h;dirty=true;int rw=std::max(1,w/2),rh=std::max(1,h/2);glBindTexture(GL_TEXTURE_2D,renderTex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rw,rh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderTex,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("framebuffer incomplete");}
  if(now-timer>=1.0){fps=frames/(now-timer);frames=0;timer=now;dirty=true;}if(dirty){if(hud)glDeleteTextures(1,&hud);hud=makeHUD(w,h);dirty=false;}
  int rw=std::max(1,w/2),rh=std::max(1,h/2);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,rw,rh);glDisable(GL_BLEND);glUseProgram(scene);glUniform2f(glGetUniformLocation(scene,"resolution"),rw,rh);glUniform1f(glGetUniformLocation(scene,"time"),(float)simTime);glUniform1f(glGetUniformLocation(scene,"yaw"),yaw);glUniform1f(glGetUniformLocation(scene,"inclination"),inclination);glUniform1f(glGetUniformLocation(scene,"zoom"),zoom);glUniform1i(glGetUniformLocation(scene,"mode"),mode);glUniform1i(glGetUniformLocation(scene,"diskEnabled"),disk);glDrawArrays(GL_TRIANGLES,0,3);
  glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,w,h);glUseProgram(blit);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,renderTex);glUniform1i(glGetUniformLocation(blit,"image"),0);glUniform1i(glGetUniformLocation(blit,"flip"),0);glDrawArrays(GL_TRIANGLES,0,3);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glBindTexture(GL_TEXTURE_2D,hud);glUniform1i(glGetUniformLocation(blit,"flip"),1);glDrawArrays(GL_TRIANGLES,0,3);glDisable(GL_BLEND);
  glfwSwapBuffers(window);glfwPollEvents();frames++;totalFrames++;if(benchmark&&totalFrames>=180){glFinish();std::cout<<"Average FPS: "<<totalFrames/(glfwGetTime()-start)<<" ("<<w<<"x"<<h<<" framebuffer, half-resolution ray tracing)\n";break;}
 }
 glDeleteTextures(1,&hud);glDeleteTextures(1,&renderTex);glDeleteFramebuffers(1,&fbo);glDeleteProgram(scene);glDeleteProgram(blit);glDeleteVertexArrays(1,&vao);glfwDestroyWindow(window);glfwTerminate();return 0;
 }catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<"\n";return 1;}}
