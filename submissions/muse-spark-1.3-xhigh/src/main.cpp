// black-hole-cpp-muse-1-3 / src/main.cpp
// Realtime Schwarzschild black-hole simulation:
//   * GPU null-geodesic ray tracing  -> gravitational lensing + shadow
//   * Thin Novikov-Thorne accretion disk (Doppler + gravitational redshift)
//   * Photon-ring / halo higher-order images (emerge from the integrator)
//   * Flamm-paraboloid spacetime grid ("trapdoor in spacetime" view)
// Realtime via OpenGL 4.1 + GLSL (GPU). No third-party math libs; only GLFW.
//
// Controls (also printed with -h / H):
//   mouse-drag orbit | wheel zoom | 1/2/G switch view | B bending on/off
//   D doppler on/off | K disk on/off | +/- steps | [/] step size
//   Space pause disk | R reset cam | P screenshot (PPM) | H help | ESC quit
#include "physics.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------- tiny vec/mat (column-major Mat4 like OpenGL) -------------
struct V3 { float x, y, z; };
static V3 v3(float x,float y,float z){return{x,y,z};}
struct V3UnusedHelper { static V3 add(V3 a,V3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};} };
static V3 vsub(V3 a,V3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static V3 vmul(V3 a,float s){return{a.x*s,a.y*s,a.z*s};}
static float vdot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 vcross(V3 a,V3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static float vlen(V3 a){return std::sqrt(vdot(a,a));}
static V3 vnorm(V3 a){float l=vlen(a);return l>1e-9f?vmul(a,1.0f/l):a;}

struct M4 { float m[16]; }; // column-major
static M4 mIdent(){M4 o{};o.m[0]=o.m[5]=o.m[10]=o.m[15]=1.0f;return o;}
static M4 mMul(const M4&a,const M4&b){M4 o{};for(int c=0;c<4;++c)for(int r=0;r<4;++r){float s=0;for(int k=0;k<4;++k)s+=a.m[k*4+r]*b.m[c*4+k];o.m[c*4+r]=s;}return o;}
static M4 mPersp(float fovDeg,float aspect,float zn,float zf){
    float f=1.0f/std::tan(fovDeg*0.5f*(float)M_PI/180.0f);
    M4 o{};o.m[0]=f/aspect;o.m[5]=f;o.m[10]=(zf+zn)/(zn-zf);o.m[11]=-1.0f;o.m[14]=2.0f*zf*zn/(zn-zf);return o;}
static M4 mLook(V3 eye,V3 tgt,V3 up){
    V3 f=vnorm(vsub(tgt,eye)),s=vnorm(vcross(f,up)),u=vcross(s,f);
    M4 o=mIdent();o.m[0]=s.x;o.m[1]=u.x;o.m[2]=-f.x;o.m[4]=s.y;o.m[5]=u.y;
    o.m[6]=-f.y;o.m[8]=s.z;o.m[9]=u.z;o.m[10]=-f.z;
    o.m[12]=-vdot(s,eye);o.m[13]=-vdot(u,eye);o.m[14]=vdot(f,eye);return o;}

// ---------------- app state -------------------------------------------------
struct State {
    int mode = 0;            // 0 = black hole, 1 = spacetime funnel
    float yaw = 0.0f, pitch = 0.28f, dist = 11.0f;
    bool bending = true, doppler = true, disk = true;
    int steps = 220;
    float stepScale = 1.0f, exposure = 1.15f, bgGain = 1.0f;
    bool paused = false, autoQ = true;
    double simTime = 0.0;
    bool drag = false; double lastX = 0, lastY = 0;
};
static State g;

// ---------------- shader helpers --------------------------------------------
static std::string exeDir;
static std::string findShader(const std::string& name) {
    std::vector<std::string> cands = {
        exeDir + "/shaders/" + name, "./shaders/" + name,
        "../shaders/" + name, "black-hole-cpp-muse-1-3/shaders/" + name,
        std::string(__FILE__).substr(0, std::string(__FILE__).find_last_of("/\\")) + "/../shaders/" + name,
    };
    for (auto& c : cands) { std::ifstream f(c); if (f.good()) return c; }
    return "shaders/" + name;
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str();
}
static GLuint compile(GLenum type, const std::string& src, const std::string& tag) {
    GLuint sh = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[8192]; glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        std::cerr << "SHADER COMPILE FAIL [" << tag << "]:\n" << log << "\n"; std::exit(1); }
    return sh;
}
static GLuint linkProg(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::cerr << "PROGRAM LINK FAIL:\n" << log << "\n"; std::exit(1); }
    return p;
}
static GLuint loadProg(const std::string& vsName, const std::string& fsName) {
    std::string vp = findShader(vsName), fp = findShader(fsName);
    GLuint vs = compile(GL_VERTEX_SHADER, readFile(vp), vp);
    GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(fp), fp);
    GLuint p = linkProg(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}
static void setVec3(GLuint pr, const char* n, V3 v){glUniform3f(glGetUniformLocation(pr,n),v.x,v.y,v.z);}
static void setF(GLuint pr, const char* n, float v){glUniform1f(glGetUniformLocation(pr,n),v);}
static void setI(GLuint pr, const char* n, int v){glUniform1i(glGetUniformLocation(pr,n),v);}

// ---------------- spacetime funnel mesh (Flamm's paraboloid) ----------------
// Equatorial slice embedding: w(r) = 2*sqrt(Rs*(r-Rs)); rendered as a polar
// grid of rings + spokes at y = -w(r). Color encodes Kretschmann K=48M^2/r^6
// (log-mapped deep-blue -> cyan -> white toward the throat).
struct FVertex { float x, y, z, r, g, b; };
static void pushSeg(std::vector<FVertex>& v, float ax,float ay,float az, float bx,float by,float bz,
                    float ar,float ag,float ab, float br,float bg,float bb){
    v.push_back({ax,ay,az,ar,ag,ab}); v.push_back({bx,by,bz,br,bg,bb});
}
static void curvatureColor(double r, float& R, float& G, float& B) {
    double k = bh::kretschmann(r);              // 12 at horizon .. ~0 far
    double t = std::log10(1.0 + k) / std::log10(13.0); // 1 at throat
    if (t < 0) t = 0; if (t > 1) t = 1;
    R = (float)(0.10 + 0.90 * t * t);
    G = (float)(0.18 + 0.62 * t);
    B = (float)(0.45 + 0.55 * (1.0 - t) * 0.4 + 0.45 * t);
}
static std::vector<FVertex> buildFunnelLines() {
    std::vector<FVertex> v;
    const double Rs = bh::RS, rMax = 10.0;
    const int nRings = 30, nSpokes = 48, nSeg = 128;
    // log-spaced ring radii from just outside horizon to rMax
    std::vector<double> rings;
    for (int i = 0; i < nRings; ++i) {
        double t = (double)i / (nRings - 1);
        rings.push_back(1.02 + (rMax - 1.02) * std::pow(t, 1.6));
    }
    for (double r : rings) {
        double w = bh::flammW(r);
        float R, G, B; curvatureColor(r, R, G, B);
        // brighten the three physically marked radii
        float boost = 1.0f;
        if (std::fabs(r - bh::R_ISCO) < 0.09) { R = 1.0f; G = 0.55f; B = 0.15f; boost = 1.0f; }
        if (std::fabs(r - bh::R_PHOTON) < 0.05) { R = 1.0f; G = 0.9f; B = 0.2f; boost = 1.0f; }
        for (int i = 0; i < nSeg; ++i) {
            double a0 = 2*M_PI*i/nSeg, a1 = 2*M_PI*(i+1)/nSeg;
            pushSeg(v, (float)(r*cos(a0)), (float)-w, (float)(r*sin(a0)),
                       (float)(r*cos(a1)), (float)-w, (float)(r*sin(a1)),
                       R*boost, G*boost, B*boost, R*boost, G*boost, B*boost);
        }
    }
    for (int s = 0; s < nSpokes; ++s) {
        double a = 2*M_PI*s/nSpokes;
        double ca = cos(a), sa = sin(a);
        for (size_t i = 0; i + 1 < rings.size(); ++i) {
            double r0 = rings[i], r1 = rings[i+1];
            float R0,G0,B0,R1,G1,B1;
            curvatureColor(r0,R0,G0,B0); curvatureColor(r1,R1,G1,B1);
            R0*=0.55f;G0*=0.55f;B0*=0.55f;R1*=0.55f;G1*=0.55f;B1*=0.55f;
            pushSeg(v, (float)(r0*ca), (float)-bh::flammW(r0), (float)(r0*sa),
                       (float)(r1*ca), (float)-bh::flammW(r1), (float)(r1*sa),
                       R0,G0,B0,R1,G1,B1);
        }
    }
    // event-horizon rim (red) + photon ring (yellow) + ISCO ring (orange)
    auto ring = [&](double r, float R, float G, float B){
        double w = bh::flammW(r);
        for (int i = 0; i < nSeg; ++i) {
            double a0=2*M_PI*i/nSeg, a1=2*M_PI*(i+1)/nSeg;
            pushSeg(v,(float)(r*cos(a0)),(float)-w+0.01f,(float)(r*sin(a0)),
                      (float)(r*cos(a1)),(float)-w+0.01f,(float)(r*sin(a1)),R,G,B,R,G,B);
        }};
    ring(bh::R_HORIZON, 1.0f, 0.15f, 0.15f);
    ring(bh::R_PHOTON, 1.0f, 0.9f, 0.2f);
    ring(bh::R_ISCO, 1.0f, 0.5f, 0.1f);
    (void)Rs;
    return v;
}

// ---------------- headless physics self-test --------------------------------
static int runSelfTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* name, const std::string& detail){
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "  " << detail << "\n";
        if (!ok) ++fails;
    };
    check(std::fabs(bh::flammW(1.0)) < 1e-12, "flamm-throat",
          "w(Rs)=" + std::to_string(bh::flammW(1.0)) + " (expect 0)");
    check(std::fabs(bh::flammW(2.0) - 2.0) < 1e-12, "flamm-r2",
          "w(2Rs)=" + std::to_string(bh::flammW(2.0)) + " (expect 2)");
    check(std::fabs(bh::R_PHOTON - 1.5) < 1e-12 && std::fabs(bh::R_ISCO - 3.0) < 1e-12,
          "radii", "photon=1.5Rs isco=3Rs");
    check(std::fabs(bh::B_CRIT - 2.59807621135) < 1e-6, "shadow-radius",
          "b_c=" + std::to_string(bh::B_CRIT) + " (expect 3*sqrt(3)/2)");
    check(bh::diskFluxShape(2.0) == 0.0 && bh::diskFluxShape(4.0) > 0.2, "disk-flux",
          "F(2Rs)=0 F(4Rs)=" + std::to_string(bh::diskFluxShape(4.0)));
    check(std::fabs(bh::shellOrbitalSpeed(bh::R_ISCO) - 0.5) < 0.02, "isco-speed",
          "v(6M)=" + std::to_string(bh::shellOrbitalSpeed(3.0)) + " (expect ~0.5c)");
    // lensing: parallel rays along -z with impact parameter b = sqrt(x^2+y^2).
    // (Aimed at infinity, NOT at the origin, so b is the true impact parameter.)
    auto shoot = [&](double b, bool bend){
        bh::Vec3 p{ b, 0.5, 12.0 };
        bh::Vec3 v{ 0.0, 0.0, -1.0 };
        return bh::tracePhotonCPU(p, v, bend, 800);
    };
    bool capNear = shoot(1.0, true), escFar = !shoot(8.0, true);
    check(capNear, "capture-near", "b=1Rs ray must fall in");
    check(escFar, "escape-far", "b=8Rs ray must escape");
    // key lensing claim: b=2.0 (< b_c) is captured WITH bending but flies
    // straight past WITHOUT bending -> proves the bend term lenses.
    bool bentCap = shoot(2.0, true), straightEsc = !shoot(2.0, false);
    check(bentCap && straightEsc, "lensing-bends",
          "b=2Rs bent=" + std::string(bentCap?"captured":"escaped") +
          " straight=" + std::string(straightEsc?"escaped":"captured"));
    std::cout << (fails ? "SELF-TEST FAILED\n" : "SELF-TEST PASSED\n");
    return fails ? 1 : 0;
}
static void printHelp(const char* argv0) {
    std::cout <<
    "blackhole — realtime Schwarzschild BH (OpenGL/GLSL, G=c=1, Rs=1)\n"
    "usage: " << argv0 << " [--test] [--spacetime] [--no-bending] [--steps N] [-h]\n"
    "  --test       run CPU physics self-test (no window) and exit\n"
    "  --spacetime  start in spacetime-grid view\n"
    "  --no-bending start with straight-line rays (lensing comparison)\n"
    "  --steps N    geodesic steps per pixel (80..320, default 220)\n"
    "keys: 1 BH view | 2/G spacetime grid | B bending | D doppler | K disk\n"
    "      +/- steps | [ ] step size | Space pause | R reset | P shot | H help\n";
}

// ---------------- input -----------------------------------------------------
static void keyCb(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_1: g.mode = 0; break;
        case GLFW_KEY_2: g.mode = 1; break;
        case GLFW_KEY_G: g.mode = 1 - g.mode; break;
        case GLFW_KEY_B: g.bending = !g.bending;
            std::cout << "bending " << (g.bending?"ON (GR geodesics)":"OFF (straight lines)") << "\n"; break;
        case GLFW_KEY_D: g.doppler = !g.doppler;
            std::cout << "doppler/redshift " << (g.doppler?"ON":"OFF") << "\n"; break;
        case GLFW_KEY_K: g.disk = !g.disk;
            std::cout << "disk " << (g.disk?"ON":"OFF") << "\n"; break;
        case GLFW_KEY_EQUAL: case GLFW_KEY_KP_ADD:
            g.steps += 20; if (g.steps > 320) g.steps = 320;
            std::cout << "steps " << g.steps << "\n"; break;
        case GLFW_KEY_MINUS: case GLFW_KEY_KP_SUBTRACT:
            g.steps -= 20; if (g.steps < 60) g.steps = 60;
            std::cout << "steps " << g.steps << "\n"; break;
        case GLFW_KEY_LEFT_BRACKET: g.stepScale *= 0.85f; if (g.stepScale<0.4f) g.stepScale=0.4f; break;
        case GLFW_KEY_RIGHT_BRACKET: g.stepScale *= 1.18f; if (g.stepScale>2.5f) g.stepScale=2.5f; break;
        case GLFW_KEY_SPACE: g.paused = !g.paused; break;
        case GLFW_KEY_A: g.autoQ = !g.autoQ;
            std::cout << "auto-quality " << (g.autoQ?"ON":"OFF") << "\n"; break;
        case GLFW_KEY_R: g.yaw = 0; g.pitch = 0.28f; g.dist = 11.0f; break;
        case GLFW_KEY_H: printHelp("blackhole"); break;
        case GLFW_KEY_P: {
            int W, H; glfwGetFramebufferSize(w, &W, &H);
            std::vector<unsigned char> px((size_t)W*H*3);
            glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            char name[128]; std::snprintf(name, sizeof name, "/tmp/blackhole_%dx%d.ppm", W, H);
            std::ofstream f(name, std::ios::binary);
            f << "P6\n" << W << " " << H << "\n255\n";
            for (int y = H-1; y >= 0; --y) f.write((char*)&px[(size_t)y*W*3], (size_t)W*3);
            std::cout << "screenshot " << name << "\n"; break;
        }
    }
}
static void mouseBtnCb(GLFWwindow* w, int b, int a, int) {
    if (b == GLFW_MOUSE_BUTTON_LEFT) {
        g.drag = (a == GLFW_PRESS);
        glfwGetCursorPos(w, &g.lastX, &g.lastY);
    }
}
static void cursorCb(GLFWwindow*, double x, double y) {
    if (!g.drag) return;
    g.yaw   -= (float)(x - g.lastX) * 0.005f;
    g.pitch += (float)(y - g.lastY) * 0.005f;
    if (g.pitch > 1.45f) g.pitch = 1.45f;
    if (g.pitch < -1.45f) g.pitch = -1.45f;
    g.lastX = x; g.lastY = y;
}
static void scrollCb(GLFWwindow*, double, double dy) {
    g.dist *= (float)std::pow(1.12, -dy);
    if (g.dist < 4.5f) g.dist = 4.5f;
    if (g.dist > 40.0f) g.dist = 40.0f;
}
static V3 orbitCam(V3 target) {
    float cp = std::cos(g.pitch), sp = std::sin(g.pitch);
    return { target.x + g.dist*cp*(float)std::sin(g.yaw),
             target.y + g.dist*sp,
             target.z + g.dist*cp*(float)std::cos(g.yaw) };
}

// ---------------- main ------------------------------------------------------
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--test") return runSelfTest();
        if (a == "--spacetime" || a == "-s") g.mode = 1;
        else if (a == "--no-bending") g.bending = false;
        else if ((a == "--steps" || a == "-n") && i + 1 < argc) {
            g.steps = std::atoi(argv[++i]);
            if (g.steps < 60) g.steps = 60; if (g.steps > 320) g.steps = 320;
        } else if (a == "-h" || a == "--help") { printHelp(argv[0]); return 0; }
    }
    { std::string p = argv[0];
      auto s = p.find_last_of("/\\"); exeDir = (s == std::string::npos) ? "." : p.substr(0, s); }

    if (!glfwInit()) { std::cerr << "glfwInit failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* win = glfwCreateWindow(1280, 800, "Black Hole — Schwarzschild ray tracer (1: BH  2: spacetime  H: help)", nullptr, nullptr);
    if (!win) { std::cerr << "window creation failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, keyCb);
    glfwSetMouseButtonCallback(win, mouseBtnCb);
    glfwSetCursorPosCallback(win, cursorCb);
    glfwSetScrollCallback(win, scrollCb);

    std::cout << "Black-hole realtime sim  |  1 BH view, 2/G spacetime grid, B bending, H help\n"
                 "Rs=1 horizon | photon sphere 1.5 | ISCO/disk-in 3 | disk-out 9 | shadow ~2.6\n";

    GLuint bhProg = loadProg("fullscreen.vert", "blackhole.frag");
    GLuint funProg = loadProg("funnel.vert", "funnel.frag");

    // fullscreen triangle
    GLuint fsVAO = 0, fsVBO = 0;
    { float tri[6] = {-1,-1, 3,-1, -1,3};
      glGenVertexArrays(1, &fsVAO); glBindVertexArray(fsVAO);
      glGenBuffers(1, &fsVBO); glBindBuffer(GL_ARRAY_BUFFER, fsVBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0); }

    // funnel line mesh
    GLuint funVAO = 0, funVBO = 0; GLsizei funN = 0;
    { auto lines = buildFunnelLines(); funN = (GLsizei)lines.size();
      glGenVertexArrays(1, &funVAO); glBindVertexArray(funVAO);
      glGenBuffers(1, &funVBO); glBindBuffer(GL_ARRAY_BUFFER, funVBO);
      glBufferData(GL_ARRAY_BUFFER, lines.size()*sizeof(FVertex), lines.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)(3*sizeof(float))); }

    double tPrev = glfwGetTime(), fpsT = tPrev;
    int fpsN = 0, fps = 0;
    while (!glfwWindowShouldClose(win)) {
        double tNow = glfwGetTime();
        double dt = tNow - tPrev; tPrev = tNow;
        if (!g.paused) g.simTime += dt;
        fpsN++; if (tNow - fpsT >= 0.5) { fps = (int)(fpsN / (tNow - fpsT)); fpsN = 0; fpsT = tNow; }

        // gentle auto-orbit when idle (stops while dragging)
        if (!g.drag) g.yaw += (float)dt * 0.05;

        int W, H; glfwGetFramebufferSize(win, &W, &H);
        glViewport(0, 0, W, H);

        if (g.mode == 0) {
            V3 target{0, 0, 0};
            V3 eye = orbitCam(target);
            V3 fwd = vnorm(vsub(target, eye));
            V3 right = vnorm(vcross(fwd, v3(0, 1, 0)));
            V3 up = vcross(right, fwd);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(bhProg);
            setF(bhProg, "uTime", (float)g.simTime);
            glUniform2f(glGetUniformLocation(bhProg, "uResolution"), (float)W, (float)H);
            setVec3(bhProg, "uCamPos", eye);
            setVec3(bhProg, "uCamFwd", fwd);
            setVec3(bhProg, "uCamRight", right);
            setVec3(bhProg, "uCamUp", up);
            setF(bhProg, "uFovDeg", 55.0f);
            setF(bhProg, "uRs", 1.0f);
            setI(bhProg, "uSteps", g.steps);
            setF(bhProg, "uStepScale", g.stepScale);
            setI(bhProg, "uBending", g.bending ? 1 : 0);
            setI(bhProg, "uDoppler", g.doppler ? 1 : 0);
            setI(bhProg, "uDisk", g.disk ? 1 : 0);
            setF(bhProg, "uExposure", g.exposure);
            setF(bhProg, "uBgGain", g.bgGain);
            glBindVertexArray(fsVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glEnable(GL_DEPTH_TEST);
            glClearColor(0.004f, 0.005f, 0.012f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            V3 target{0, -2.4f, 0};
            V3 eye = orbitCam(target);
            M4 proj = mPersp(55.0f, W / (float)(H > 0 ? H : 1), 0.1f, 200.0f);
            M4 view = mLook(eye, target, v3(0, 1, 0));
            M4 mvp = mMul(proj, view);
            glUseProgram(funProg);
            glUniformMatrix4fv(glGetUniformLocation(funProg, "uMVP"), 1, GL_FALSE, mvp.m);
            glBindVertexArray(funVAO);
            glDrawArrays(GL_LINES, 0, funN);
            // animated infalling probe: radial plunge with eased proper-time feel
            double cyc = std::fmod(g.simTime * 0.25, 1.0);
            double rp = 10.0 - 8.9 * cyc * cyc;
            double ap = 1.2 + g.simTime * 0.35;
            float px = (float)(rp * std::cos(ap)), pz = (float)(rp * std::sin(ap));
            float py = (float)-bh::flammW(rp) + 0.05f;
            glPointSize(7.0f);
            GLfloat dot[6] = {px, py, pz, 1.0f, 0.75f, 0.2f};
            GLuint dotVAO = 0, dotVBO = 0;
            glGenVertexArrays(1, &dotVAO); glBindVertexArray(dotVAO);
            glGenBuffers(1, &dotVBO); glBindBuffer(GL_ARRAY_BUFFER, dotVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof dot, dot, GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
            glDrawArrays(GL_POINTS, 0, 1);
            glDeleteBuffers(1, &dotVBO); glDeleteVertexArrays(1, &dotVAO);
            glBindVertexArray(funVAO);
        }

        // realtime auto-quality: hold ~30-60 fps by trimming steps
        if (g.autoQ && g.mode == 0 && fps > 0) {
            static double acc = 0; acc += dt;
            if (acc > 1.5) {
                acc = 0;
                if (fps < 28 && g.steps > 100) { g.steps -= 20; }
                else if (fps > 57 && g.steps < 300) { g.steps += 10; }
            }
        }
        char title[256];
        std::snprintf(title, sizeof title,
            "BH Rs=1 | %s | %dfps steps=%d | bend %s dop %s disk %s | 1/2 view B/D/K H help",
            g.mode == 0 ? "lensing+disk" : "spacetime grid",
            fps, g.steps, g.bending ? "on" : "OFF", g.doppler ? "on" : "off",
            g.disk ? "on" : "off");
        glfwSetWindowTitle(win, title);
        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}
