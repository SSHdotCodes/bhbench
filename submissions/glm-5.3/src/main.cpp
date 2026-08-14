// ============================================================================
//  blackhole — real-time Schwarzschild black hole simulator
//  OpenGL 4.1 core | GLFW | per-pixel null-geodesic ray tracing (GPU)
//
//  Units: rs = 1, G = c = 1, M = 1/2.  See shaders/blackhole.frag for the
//  physics notes.
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#define GL_SILENCE_DEPRECATION 1
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

// --------------------------------------------------------------- utilities
static const char* kQuadVert = R"(#version 410 core
out vec2 vUV;
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p*2.0 - 1.0, 0.0, 1.0);
})";

static const char* kBlitFrag = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 o;
void main(){ o = texture(uTex, vUV*0.5); }
)";

static GLuint compile(GLenum type, const char* src, const char* label){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok){
        char log[8192];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        std::fprintf(stderr, "[shader error: %s]\n%.*s\n", label, n, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(GLuint vs, GLuint fs, const char* label){
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok){
        char log[8192];
        GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof(log), &n, log);
        std::fprintf(stderr, "[link error: %s]\n%.*s\n", label, n, log);
        return 0;
    }
    return p;
}

static std::string loadFile(const char* path){
    FILE* f = std::fopen(path, "rb");
    if (!f) return "";
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string s(len, '\0');
    if (std::fread(&s[0], 1, len, f) != (size_t)len) { std::fclose(f); return ""; }
    std::fclose(f);
    return s;
}

// ------------------------------------------------------------- app state
struct State {
    float yaw = 2.35f, pitch = 0.14f, dist = 11.5f;   // smoothed
    float tYaw = 2.35f, tPitch = 0.14f, tDist = 11.5f; // targets
    int   mode = 0;        // 0 realistic, 1 grid, 2 both
    int   quality = 1;     // 0/1/2
    int   resIdx = 1;
    bool  disk = true, stars = true, paused = false;
    float animTime = 0.0f, animSpeed = 1.0f;
    float exposure = 1.1f, diskTemp = 6400.0f;
    bool  drag = false;
    double lx = 0, ly = 0;
    double benchEnd = 0.0;
    bool  shouldClose = false;
};
static State S;

static const int   kSteps[3]    = {320, 560, 900};
static const float kDtScale[3]  = {1.00f, 0.80f, 0.60f};
static const float kResScales[] = {0.50f, 0.66f, 0.80f, 1.00f};
static const int   kNumRes = 4;

static GLFWwindow* gWin = nullptr;
static int gWinW = 1280, gWinH = 860;    // framebuffer pixels (retina-aware)
static GLuint gFbo = 0, gTex = 0;
static int gFw = 0, gFh = 0;
static GLuint gMainProg = 0, gBlitProg = 0;
static GLint gBlitTexLoc = 0;

// offscreen mode: render without a visible window, save BMP, exit
static bool gOff = false;
static int gOffW = 960, gOffH = 640, gOffFrames = 1;
static std::string gOffOut;

// main-program uniform locations
#define U(name) GLint u_##name = -1;
U(Res) U(Anim) U(CamPos) U(CamBasis) U(TanHalfFov) U(Mode) U(DiskOn) U(StarsOn)
U(Steps) U(DtScale) U(Exposure) U(DiskTemp)
#undef U

static void ensureFBO(){
    float scale = gOff ? 1.0f : kResScales[S.resIdx];
    int w = std::max(4, (int)std::lround(gWinW*scale));
    int h = std::max(4, (int)std::lround(gWinH*scale));
    if (w == gFw && h == gFh && gFbo) return;
    if (gTex){ glDeleteTextures(1, &gTex); gTex = 0; }
    if (gFbo){ glDeleteFramebuffers(1, &gFbo); gFbo = 0; }
    glGenTextures(1, &gTex);
    glBindTexture(GL_TEXTURE_2D, gTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &gFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gFw = w; gFh = h;
}

// ------------------------------------------------------- BMP output (24-bit)
static bool writeBMP(const char* path, int w, int h, const unsigned char* rgba){
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    int rowBytes = (w*3 + 3) & ~3;
    unsigned int dataSz = (unsigned int)(rowBytes*h);
    unsigned int fileSz = 54 + dataSz;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSz, 4);
    unsigned int off = 54;  std::memcpy(hdr + 10, &off, 4);
    unsigned int hs = 40;   std::memcpy(hdr + 14, &hs, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    short planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &dataSz, 4);
    int res = 2835;
    std::memcpy(hdr + 38, &res, 4);
    std::memcpy(hdr + 42, &res, 4);
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row(rowBytes, 0);
    for (int y = h - 1; y >= 0; y--){          // flip to bottom-up BGR
        for (int x = 0; x < w; x++){
            row[x*3 + 0] = rgba[(y*w + x)*4 + 2];
            row[x*3 + 1] = rgba[(y*w + x)*4 + 1];
            row[x*3 + 2] = rgba[(y*w + x)*4 + 0];
        }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
    return true;
}

// ---------------------------------------------------------------- camera
// Orthonormal camera frame: pos, and basis columns (right, up, forward)
// looking at the origin.  right = normalize(cross(fwd, worldUp)),
// up = cross(right, fwd).
static void camFrame(float yaw, float pitch, float dist,
                     float* pos, float* basis){
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float px = dist*cp*sy, py = dist*sp, pz = dist*cp*cy;
    float fl = std::sqrt(px*px + py*py + pz*pz);
    float fx = -px/fl, fy = -py/fl, fz = -pz/fl;
    float rx = -fz, rz = fx;
    float rl = std::sqrt(rx*rx + rz*rz);
    rx /= rl; rz /= rl;
    float ux = -rz*fy, uy = rz*fx - rx*fz, uz = rx*fy;
    pos[0] = px; pos[1] = py; pos[2] = pz;
    basis[0] = rx; basis[1] = 0.0f; basis[2] = rz;     // column 0: right
    basis[3] = ux; basis[4] = uy;    basis[5] = uz;    // column 1: up
    basis[6] = fx; basis[7] = fy;    basis[8] = fz;    // column 2: forward
}

// ------------------------------------------------------------- callbacks
static void onFramebuf(GLFWwindow*, int w, int h){
    gWinW = w; gWinH = h;
    ensureFBO();
}

static void onKey(GLFWwindow* w, int key, int, int action, int){
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    switch (key){
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_G: S.mode = (S.mode + 1) % 3; break;
        case GLFW_KEY_V: S.disk = !S.disk; break;
        case GLFW_KEY_B: S.stars = !S.stars; break;
        case GLFW_KEY_SPACE: S.paused = !S.paused; break;
        case GLFW_KEY_1: S.quality = 0; break;
        case GLFW_KEY_2: S.quality = 1; break;
        case GLFW_KEY_3: S.quality = 2; break;
        case GLFW_KEY_F2: if (S.resIdx > 0) S.resIdx--; ensureFBO(); break;
        case GLFW_KEY_F3: if (S.resIdx < kNumRes - 1) S.resIdx++; ensureFBO(); break;
        case GLFW_KEY_LEFT_BRACKET:  S.diskTemp = std::max(2500.0f, S.diskTemp - 400.0f); break;
        case GLFW_KEY_RIGHT_BRACKET: S.diskTemp = std::min(20000.0f, S.diskTemp + 400.0f); break;
        case GLFW_KEY_MINUS: S.exposure = std::max(0.1f, S.exposure*0.85f); break;
        case GLFW_KEY_EQUAL: S.exposure = std::min(6.0f, S.exposure*1.15f); break;
        case GLFW_KEY_COMMA:  S.animSpeed = std::max(0.0f, S.animSpeed*0.75f); break;
        case GLFW_KEY_PERIOD: S.animSpeed = std::min(8.0f, S.animSpeed*1.3f); break;
        case GLFW_KEY_R:
            S.tYaw = 2.35f; S.tPitch = 0.14f; S.tDist = 11.5f; break;
    }
}

static void onMouseButton(GLFWwindow*, int btn, int action, int){
    if (btn == GLFW_MOUSE_BUTTON_LEFT){
        S.drag = (action == GLFW_PRESS);
        if (S.drag) glfwGetCursorPos(gWin, &S.lx, &S.ly);
    }
}

static void onCursor(GLFWwindow*, double x, double y){
    if (!S.drag) return;
    float dx = (float)(x - S.lx), dy = (float)(y - S.ly);
    S.lx = x; S.ly = y;
    S.tYaw   += dx*0.005f;
    S.tPitch = std::max(-1.45f, std::min(1.45f, S.tPitch + dy*0.005f));
}

static void onScroll(GLFWwindow*, double, double y){
    S.tDist = std::max(2.2f, std::min(60.0f, S.tDist*(float)std::pow(1.15, -y)));
}

// ------------------------------------------------------------------- main
int main(int argc, char** argv){
    for (int i = 1; i < argc; i++){
        if (!std::strcmp(argv[i], "--benchmark") && i + 1 < argc){
            S.benchEnd = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc){
            S.mode = std::atoi(argv[++i]) % 3;
        } else if (!std::strcmp(argv[i], "--quality") && i + 1 < argc){
            S.quality = std::max(0, std::min(2, std::atoi(argv[++i])));
        } else if (!std::strcmp(argv[i], "--res") && i + 1 < argc){
            S.resIdx = std::max(0, std::min(kNumRes - 1, std::atoi(argv[++i])));
        } else if (!std::strcmp(argv[i], "--offscreen") && i + 4 < argc){
            gOff = true;
            gOffW = std::max(16, std::atoi(argv[++i]));
            gOffH = std::max(16, std::atoi(argv[++i]));
            gOffFrames = std::max(1, std::atoi(argv[++i]));
            gOffOut = argv[++i];
        }
    }

    glfwSetErrorCallback([](int c, const char* d){ std::fprintf(stderr, "GLFW %d: %s\n", c, d); });
    if (!glfwInit()){ std::fprintf(stderr, "glfwInit failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, 1);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_VISIBLE, gOff ? GLFW_FALSE : GLFW_TRUE);

    gWin = glfwCreateWindow(gOff ? gOffW : 1280, gOff ? gOffH : 860,
        "Schwarzschild Black Hole — Geodesic Ray Tracer", nullptr, nullptr);
    if (!gWin){ std::fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(gWin);
    glfwSwapInterval(1);

    std::printf("GL renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    // a bound VAO is required for attributeless draws on core profile
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // shaders -----------------------------------------------------------
    GLuint quadVs = compile(GL_VERTEX_SHADER, kQuadVert, "quad.vert");
    GLuint blitFs = compile(GL_FRAGMENT_SHADER, kBlitFrag, "blit.frag");
    gBlitProg = link(quadVs, blitFs, "blit");
    gBlitTexLoc = glGetUniformLocation(gBlitProg, "uTex");

    std::string frag = loadFile("shaders/blackhole.frag");
    if (frag.empty()) frag = loadFile("../shaders/blackhole.frag");
    if (frag.empty()){
        std::fprintf(stderr, "cannot open shaders/blackhole.frag (run from the project root)\n");
        return 1;
    }
    GLuint mainFs = compile(GL_FRAGMENT_SHADER, frag.c_str(), "blackhole.frag");
    if (!mainFs) return 1;
    gMainProg = link(quadVs, mainFs, "main");
    if (!gMainProg) return 1;

#define U(name) u_##name = glGetUniformLocation(gMainProg, "u" #name);
    U(Res) U(Anim) U(CamPos) U(CamBasis) U(TanHalfFov) U(Mode) U(DiskOn) U(StarsOn)
    U(Steps) U(DtScale) U(Exposure) U(DiskTemp)
#undef U

    // ---- offscreen: render fixed frames invisibly, save BMP, exit -------
    if (gOff){
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(gWin, &fw, &fh);
        if (fw <= 0 || fh <= 0){ fw = gOffW; fh = gOffH; }
        gWinW = fw; gWinH = fh;
        ensureFBO();

        float camPos[3], basis[9];
        camFrame(S.yaw, S.pitch, S.dist, camPos, basis);

        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        glViewport(0, 0, gFw, gFh);
        glUseProgram(gMainProg);
        glUniform2f(u_Res, (float)gFw, (float)gFh);
        glUniform1f(u_Anim, 2.0f);
        glUniform3f(u_CamPos, camPos[0], camPos[1], camPos[2]);
        glUniformMatrix3fv(u_CamBasis, 1, GL_FALSE, basis);
        glUniform1f(u_TanHalfFov, std::tan(27.5f*(float)M_PI/180.0f));
        glUniform1i(u_Mode, S.mode);
        glUniform1i(u_DiskOn, S.disk ? 1 : 0);
        glUniform1i(u_StarsOn, S.stars ? 1 : 0);
        glUniform1i(u_Steps, kSteps[1]);
        glUniform1f(u_DtScale, kDtScale[1]);
        glUniform1f(u_Exposure, S.exposure);
        glUniform1f(u_DiskTemp, S.diskTemp);
        for (int f = 0; f < gOffFrames; f++) glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        GLenum dbgl = glGetError();
        GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        unsigned char probe[4] = {0, 0, 0, 0};
        glReadPixels(gFw/2, gFh/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, probe);
        std::printf("[dbg] u_Res=%d u_CamPos=%d u_Steps=%d u_Mode=%d | fbo=0x%x | glerr=0x%x | center=(%d %d %d)\n",
                    u_Res, u_CamPos, u_Steps, u_Mode, fbs, dbgl, probe[0], probe[1], probe[2]);

        std::vector<unsigned char> px4((size_t)gFw*gFh*4);
        glReadPixels(0, 0, gFw, gFh, GL_RGBA, GL_UNSIGNED_BYTE, px4.data());
        bool ok = writeBMP(gOffOut.c_str(), gFw, gFh, px4.data());
        std::printf("offscreen %dx%d mode %d -> %s %s\n", gFw, gFh, S.mode,
                    gOffOut.c_str(), ok ? "[ok]" : "[FAILED]");
        glfwDestroyWindow(gWin);
        glfwTerminate();
        return ok ? 0 : 1;
    }

    // callbacks / state ---------------------------------------------------
    glfwSetFramebufferSizeCallback(gWin, onFramebuf);
    glfwSetKeyCallback(gWin, onKey);
    glfwSetMouseButtonCallback(gWin, onMouseButton);
    glfwSetCursorPosCallback(gWin, onCursor);
    glfwSetScrollCallback(gWin, onScroll);
    glfwGetFramebufferSize(gWin, &gWinW, &gWinH);
    ensureFBO();
    glDisable(GL_DEPTH_TEST);

    std::printf(
        "\nSchwarzschild black hole (rs = 1, G = c = 1)\n"
        "  mouse drag : orbit          scroll: zoom       R: reset camera\n"
        "  G : cycle view  (realistic -> spacetime grid -> combined)\n"
        "  V : accretion disk          B : star field     SPACE : pause time\n"
        "  , . : time speed            [ ] : disk temperature   - = : exposure\n"
        "  1/2/3 : integrator quality  F2/F3 : render resolution\n\n");

    double benchStart = 0.0;
    if (S.benchEnd > 0){ benchStart = glfwGetTime(); S.benchEnd += benchStart; }

    double last = glfwGetTime();
    double titleT = 0.0;
    int frames = 0, totalFrames = 0;
    const char* modeNames[3] = {"realistic", "spacetime grid", "combined"};

    while (!glfwWindowShouldClose(gWin)){
        double now = glfwGetTime();
        float dt = (float)std::min(0.1, now - last);
        last = now;
        if (!S.paused) S.animTime += dt*S.animSpeed;

        // smooth camera
        float k = 1.0f - std::exp(-12.0f*dt);
        S.yaw   += (S.tYaw - S.yaw)*k;
        S.pitch += (S.tPitch - S.pitch)*k;
        S.dist  += (S.tDist - S.dist)*k;

        float camPos[3], basis[9];
        camFrame(S.yaw, S.pitch, S.dist, camPos, basis);

        ensureFBO();
        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        glViewport(0, 0, gFw, gFh);
        glUseProgram(gMainProg);
        glUniform2f(u_Res, (float)gFw, (float)gFh);
        glUniform1f(u_Anim, S.animTime);
        glUniform3f(u_CamPos, camPos[0], camPos[1], camPos[2]);
        glUniformMatrix3fv(u_CamBasis, 1, GL_FALSE, basis);
        glUniform1f(u_TanHalfFov, std::tan(27.5f*(float)M_PI/180.0f));  // 55 deg fov
        glUniform1i(u_Mode, S.mode);
        glUniform1i(u_DiskOn, S.disk ? 1 : 0);
        glUniform1i(u_StarsOn, S.stars ? 1 : 0);
        glUniform1i(u_Steps, kSteps[S.quality]);
        glUniform1f(u_DtScale, kDtScale[S.quality]);
        glUniform1f(u_Exposure, S.exposure);
        glUniform1f(u_DiskTemp, S.diskTemp);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // upscale to window
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, gWinW, gWinH);
        glUseProgram(gBlitProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gTex);
        glUniform1i(gBlitTexLoc, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(gWin);
        glfwPollEvents();

        frames++; totalFrames++;
        if (S.benchEnd > 0 && now >= S.benchEnd) glfwSetWindowShouldClose(gWin, 1);
        if (now - titleT > 0.5){
            double fps = frames/(now - titleT);
            char title[256];
            std::snprintf(title, sizeof(title),
                "Schwarzschild Black Hole | %s | %d steps | %d%% res | disk %s | %s | %.1f fps",
                modeNames[S.mode], kSteps[S.quality],
                (int)std::lround(kResScales[S.resIdx]*100),
                S.disk ? "on" : "off", S.paused ? "paused" : "running", fps);
            glfwSetWindowTitle(gWin, title);
            titleT = now; frames = 0;
        }
    }

    if (S.benchEnd > 0){
        double avg = totalFrames/(glfwGetTime() - benchStart);
        std::printf("benchmark: %d frames, %.2f avg fps\n", totalFrames, avg);
    }
    glfwDestroyWindow(gWin);
    glfwTerminate();
    return 0;
}
