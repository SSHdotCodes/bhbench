// =============================================================================
//  Real-time Schwarzschild black-hole simulation.
//
//  Physics
//    Photons are ray-marched through the Schwarzschild metric using the null
//    geodesic equation in Cartesian form
//        a = -(3/2) Rs h^2 / r^5 * r_vec ,   h = |r x v| (conserved)
//    giving correct light bending 4GM/(c^2 b) and the photon sphere at 1.5 Rs.
//    Accretion disk: ISCO (3 Rs) .. 12 Rs, Shakura-Sunyaev T~r^-3/4,
//    Keplerian v=sqrt(Rs/2r) c, relativistic Doppler beaming I~g^4 and
//    gravitational redshift g=sqrt(1-Rs/r).
//
//  Controls
//    Mouse drag   orbit camera       Scroll  zoom
//    1            Lensed scene (stars + accretion disk + photon halo)
//    2            Lensed 3D spacetime lattice (curvature from the observer)
//    3            Flamm paraboloid embedding ("trapdoor in spacetime")
//    D            toggle accretion disk
//    G            toggle spacetime-lattice overlay
//    [ / ]        decrease / increase march quality
//    Space        pause / resume     R  reset camera      ESC  quit
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

#define GLFW_INCLUDE_GL3
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef SHADER_DIR
#define SHADER_DIR "shaders"
#endif

static std::string shaderPath(const char* name){
    return std::string(SHADER_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
static std::string readFile(const char* path){
    std::ifstream f(path, std::ios::binary);
    if(!f){ std::cerr << "Cannot open " << path << "\n"; std::exit(1); }
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static GLuint compileShader(GLenum type, const char* src, const char* name){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char log[4096]; glGetShaderInfoLog(s, 4096, nullptr, log);
        std::cerr << "Shader compile error [" << name << "]:\n" << log << "\n";
        std::exit(1);
    }
    return s;
}
static GLuint makeProgram(const char* vertPath, const char* fragPath){
    std::string vs = readFile(vertPath);
    std::string fs = readFile(fragPath);
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs.c_str(), vertPath);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs.c_str(), fragPath);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        char log[4096]; glGetProgramInfoLog(p, 4096, nullptr, log);
        std::cerr << "Link error:\n" << log << "\n"; std::exit(1);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------------------
struct Camera {
    float yaw = 0.6f, pitch = 0.25f;
    float dist = 14.0f;
    glm::vec3 target{0.0f};
    glm::vec3 pos() const {
        float cp = cosf(pitch);
        return target + dist * glm::vec3(cp*sinf(yaw), sinf(pitch), -cp*cosf(yaw));
    }
    // columns = right, up, -forward  (so ray = basis * (px,py,-focal))
    glm::mat3 basis() const {
        glm::vec3 f = glm::normalize(target - pos());
        glm::vec3 r = glm::normalize(glm::cross(glm::vec3(0,1,0), f));
        glm::vec3 u = glm::cross(f, r);
        return glm::mat3(r, u, -f);
    }
};

struct AppState {
    Camera cam;
    int    mode = 0;          // 0 scene, 1 lensed grid, 2 Flamm
    bool   showDisk = true;
    bool   showGrid = false;
    bool   paused = false;
    float  simTime = 0.0f;
    float  focal = 1.4f;
    float  Rs = 1.0f;
    float  diskInner = 3.0f;  // 3 Rs = ISCO
    float  diskOuter = 12.0f;
    bool   dragging = false;
    double lastX = 0, lastY = 0;
    GLFWwindow* win = nullptr;
};

static AppState g;

// ---------------------------------------------------------------------------
static void onKey(GLFWwindow*, int key, int, int act, int){
    if(act!=GLFW_PRESS && act!=GLFW_REPEAT) return;
    switch(key){
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(g.win, GLFW_TRUE); break;
        case GLFW_KEY_1: g.mode=0; break;
        case GLFW_KEY_2: g.mode=1; g.showGrid=true; break;
        case GLFW_KEY_3: g.mode=2; break;
        case GLFW_KEY_D: g.showDisk=!g.showDisk; break;
        case GLFW_KEY_G: g.showGrid=!g.showGrid; break;
        case GLFW_KEY_SPACE: g.paused=!g.paused; break;
        case GLFW_KEY_R: g.cam.yaw=0.6f; g.cam.pitch=0.25f; g.cam.dist=14.0f; break;
        default: break;
    }
}
static void onMouseButton(GLFWwindow*, int b, int a, int){
    if(b==GLFW_MOUSE_BUTTON_LEFT){ g.dragging=(a==GLFW_PRESS); glfwGetCursorPos(g.win,&g.lastX,&g.lastY); }
}
static void onCursor(GLFWwindow*, double x, double y){
    if(!g.dragging) return;
    double dx=x-g.lastX, dy=y-g.lastY; g.lastX=x; g.lastY=y;
    g.cam.yaw   -= float(dx)*0.005f;
    g.cam.pitch += float(dy)*0.005f;
    g.cam.pitch = std::clamp(g.cam.pitch, -1.5f, 1.5f);
}
static void onScroll(GLFWwindow*, double, double y){
    g.cam.dist *= (y<0)?1.1f:0.9f;
    g.cam.dist = std::clamp(g.cam.dist, 2.2f, 60.0f);
}

// ---------------------------------------------------------------------------
// Flamm paraboloid: z(r) = 2*sqrt(Rs*(r-Rs))
static void buildFlamm(float Rs, float rMax, int nR, int nTheta,
                       std::vector<float>& verts, std::vector<unsigned int>& idx)
{
    for(int ir=0; ir<=nR; ++ir){
        float r = Rs + (rMax - Rs) * float(ir)/float(nR);
        float z = 2.0f * sqrtf(std::max(Rs*(r-Rs), 0.0f));
        float dzdr = (r > Rs*1.0001f) ? sqrtf(Rs/(r - Rs)) : 0.0f;
        for(int it=0; it<=nTheta; ++it){
            float th = 2.0f*float(M_PI)*float(it)/float(nTheta);
            float cs=cosf(th), sn=sinf(th);
            glm::vec3 p(r*cs, -z, r*sn);            // funnel opens downward
            glm::vec3 nRz = glm::normalize(glm::vec3(dzdr, 1.0f, 0.0f));
            glm::vec3 n(nRz.x*cs, nRz.y, nRz.x*sn);
            verts.insert(verts.end(), { p.x,p.y,p.z, n.x,n.y,n.z, r, th });
        }
    }
    auto V = [&](int ir,int it){ return ir*(nTheta+1)+it; };
    for(int ir=0; ir<nR; ++ir)
        for(int it=0; it<nTheta; ++it){
            unsigned a=V(ir,it), b=V(ir+1,it), c=V(ir+1,it+1), d=V(ir,it+1);
            idx.insert(idx.end(), { a,b,d, b,c,d });
        }
}

// ---------------------------------------------------------------------------
static void glfwError(int, const char* desc){ std::cerr << "GLFW: " << desc << "\n"; }

int main(){
    glfwSetErrorCallback(glfwError);
    if(!glfwInit()){ std::cerr << "glfwInit failed\n"; return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    g.win = glfwCreateWindow(1280, 800,
        "Black Hole — Schwarzschild geodesic ray-tracer", nullptr, nullptr);
    if(!g.win){ std::cerr << "window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(g.win);
    glfwSwapInterval(1);

    glfwSetKeyCallback(g.win, onKey);
    glfwSetMouseButtonCallback(g.win, onMouseButton);
    glfwSetCursorPosCallback(g.win, onCursor);
    glfwSetScrollCallback(g.win, onScroll);

    int fbW, fbH; glfwGetFramebufferSize(g.win, &fbW, &fbH);
    glViewport(0,0,fbW,fbH);

    GLuint progBH   = makeProgram(shaderPath("fullscreen.vert").c_str(), shaderPath("blackhole.frag").c_str());
    GLuint progFlam = makeProgram(shaderPath("flamm.vert").c_str(),      shaderPath("flamm.frag").c_str());

    // fullscreen quad
    float quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
    GLuint vao, vbo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void*)0);

    // Flamm mesh
    std::vector<float> fVerts; std::vector<unsigned int> fIdx;
    buildFlamm(g.Rs, 18.0f, 140, 128, fVerts, fIdx);
    GLuint fVAO,fVBO,fEBO; glGenVertexArrays(1,&fVAO); glGenBuffers(1,&fVBO); glGenBuffers(1,&fEBO);
    glBindVertexArray(fVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fVBO);
    glBufferData(GL_ARRAY_BUFFER, fVerts.size()*sizeof(float), fVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, fIdx.size()*sizeof(unsigned int), fIdx.data(), GL_STATIC_DRAW);
    GLsizei stride = 8*sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)(6*sizeof(float)));

    std::cout <<
        "=== Black-hole simulation ===\n"
        "Mouse drag: orbit   Scroll: zoom\n"
        "1: Lensed scene   2: Lensed spacetime lattice   3: Flamm embedding\n"
        "D: disk   G: grid overlay   Space: pause   R: reset   ESC: quit\n";

    double prev = glfwGetTime();
    while(!glfwWindowShouldClose(g.win)){
        double now = glfwGetTime();
        float dt = float(now - prev); prev = now;
        if(!g.paused) g.simTime += dt;

        glfwGetFramebufferSize(g.win, &fbW, &fbH);
        glViewport(0,0,fbW,fbH);

        if(g.mode==2){
            glClearColor(0.02f,0.02f,0.04f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glUseProgram(progFlam);
            glm::mat4 view = glm::lookAt(g.cam.pos(), g.cam.target, glm::vec3(0,1,0));
            float aspect = float(fbW)/float(fbH);
            glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 200.0f);
            glm::mat4 model(1.0f);
            glUniformMatrix4fv(glGetUniformLocation(progFlam,"uView"),1,GL_FALSE,glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(progFlam,"uProj"),1,GL_FALSE,glm::value_ptr(proj));
            glUniformMatrix4fv(glGetUniformLocation(progFlam,"uModel"),1,GL_FALSE,glm::value_ptr(model));
            glUniform1f(glGetUniformLocation(progFlam,"uRs"), g.Rs);
            glUniform3fv(glGetUniformLocation(progFlam,"uCamPos"),1,glm::value_ptr(g.cam.pos()));
            glUniform1f(glGetUniformLocation(progFlam,"uTime"), g.simTime);
            glBindVertexArray(fVAO);
            glDrawElements(GL_TRIANGLES, (GLsizei)fIdx.size(), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        } else {
            glDisable(GL_DEPTH_TEST);
            glUseProgram(progBH);
            glm::vec3 cp = g.cam.pos();
            glm::mat3 basis = g.cam.basis();
            glUniform3fv(glGetUniformLocation(progBH,"uCamPos"),1,glm::value_ptr(cp));
            glUniformMatrix3fv(glGetUniformLocation(progBH,"uCamRot"),1,GL_FALSE,glm::value_ptr(basis));
            glUniform1f(glGetUniformLocation(progBH,"uFocal"), g.focal);
            glUniform1f(glGetUniformLocation(progBH,"uRs"), g.Rs);
            glUniform2f(glGetUniformLocation(progBH,"uRes"), (float)fbW,(float)fbH);
            glUniform1f(glGetUniformLocation(progBH,"uTime"), g.simTime);
            glUniform1i(glGetUniformLocation(progBH,"uMode"), g.mode);
            glUniform1i(glGetUniformLocation(progBH,"uShowDisk"), g.showDisk?1:0);
            glUniform1i(glGetUniformLocation(progBH,"uShowGrid"), g.showGrid?1:0);
            glUniform1f(glGetUniformLocation(progBH,"uDiskInner"), g.diskInner*g.Rs);
            glUniform1f(glGetUniformLocation(progBH,"uDiskOuter"), g.diskOuter*g.Rs);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        glfwSwapBuffers(g.win);
        glfwPollEvents();
    }

    glfwDestroyWindow(g.win);
    glfwTerminate();
    return 0;
}
