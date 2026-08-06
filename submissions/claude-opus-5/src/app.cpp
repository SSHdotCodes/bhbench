// app.cpp — window, camera, render graph, HUD and input.

#include "app.hpp"
#include "glutil.hpp"
#include "kerr.hpp"
#include "spectrum.hpp"
#include "geometry.hpp"
#include "mathx.hpp"
#include "text.hpp"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

namespace {

// ---------------------------------------------------------------- state ----

enum View { VIEW_RAYTRACE = 0, VIEW_FUNNEL = 1, VIEW_CONES = 2 };

struct Params {
    double spin        = 0.90;    // a/M
    double massSolar   = 1.0e7;   // black hole mass
    double eddRatio    = 0.30;    // L / L_Edd
    double diskOuter   = 24.0;    // r_g
    int    diskMode    = 2;       // 0 none, 1 opaque disk, 2 disk + halo, 3 halo only
    double turbulence  = 0.55;
    bool   physicalColor = false; // true: real Planck colour, false: remapped into the visible
    double exposure    = 0.55;
    double bloom       = 0.22;
    double skyGrid     = 0.0;
    double nebula      = 0.35;
    double starBright  = 1.0;
    double coronaBright = 0.30;
    double timeRate    = 1.0;
    int    tonemap     = 0;       // 0 asinh, 1 ACES, 2 linear
    double asinhK      = 26.0;
    bool   paused      = false;
    int    maxSteps    = 900;
    double tol         = 2.0e-6;
    double rFar        = 1200.0;
    int    debugView   = 0;
    bool   accumulate  = true;
};

struct Camera {
    double dist  = 48.0;
    double incl  = 1.396;      // polar angle of the camera, radians (80 deg)
    double azim  = 0.0;
    double fovY  = 45.0 * kerr::PI / 180.0;
};

Params   P;
Camera   Cam;
View     gView = VIEW_RAYTRACE;
bool     gShowHelp = true;
bool     gShowHud  = true;

int  gWinW = 1440, gWinH = 900;
int  gFbW = 1440, gFbH = 900;
double gRenderScale = 0.75;
int  gAccumCount = 0;
double gCoordTime = 0.0;

bool   gDragging = false;
double gLastX = 0, gLastY = 0;
bool   gDirty = true;
bool   gAnimating = false;

// ---------------------------------------------------------------- GL objects

gfx::Program progBH, progPost, progBloom, progAccum, progMesh;
gfx::RenderTarget rtRay, rtAccum[2], rtScene;
constexpr int kMaxBloom = 7;
gfx::RenderTarget bloomRT[kMaxBloom];
int gBloomLevels = 5;
int gAccumSrc = 0;

GLuint texBB = 0, texTemp = 0;
std::vector<float> tempLUT;
const int TEMP_N = 512;
const int BB_N = 1024;
const double BB_LOG_MIN = 2.3, BB_LOG_MAX = 8.6;

double gTempRef = 1.0e5;
double gTmax = 0, gLumErgS = 0, gMdot = 0, gEta = 0;
double gLutLogRIn = 0, gLutLogROut = 1;

geo::Mesh meshFunnel, meshFunnelGrid, meshRings, meshTraj, meshPhotons;
geo::Mesh meshCones, meshFloor, meshConeMarks;
bool gFunnelBuilt = false, gConesBuilt = false, gFunnelClamped = false;
double gBuiltSpin = -99;

std::string gShaderDir = SHADER_SOURCE_DIR;
std::string gShaderLog;

// ---------------------------------------------------------------- helpers ---

double diskInner() { return kerr::iscoRadius(P.spin, true); }

void markDirty() { gDirty = true; gAccumCount = 0; }

void rebuildDiskLUT() {
    gEta   = kerr::efficiency(P.spin);
    gMdot  = spectrum::accretionRate(P.massSolar, P.eddRatio, gEta);
    gLumErgS = P.eddRatio * spectrum::eddingtonLuminosity(P.massSolar);

    double rin = diskInner();
    double rout = std::max(P.diskOuter, rin * 1.2);
    gLutLogRIn  = std::log(rin);
    gLutLogROut = std::log(rout);

    tempLUT.assign(TEMP_N, 0.0f);
    gTmax = 0;
    for (int i = 0; i < TEMP_N; ++i) {
        double f = double(i) / double(TEMP_N - 1);
        double r = std::exp(gLutLogRIn + f * (gLutLogROut - gLutLogRIn));
        double F = kerr::ntFlux(r, P.spin);
        double T = spectrum::effectiveTemperature(F, P.massSolar, gMdot);
        tempLUT[i] = (float)T;
        gTmax = std::max(gTmax, T);
    }
    gTempRef = std::max(gTmax, 1.0);
    if (texTemp) gfx::updateLUT1D_R(texTemp, tempLUT, TEMP_N);
    else texTemp = gfx::makeLUT1D_R(tempLUT, TEMP_N);
    markDirty();
}

// Camera position and basis, in the pseudo-Cartesian embedding of the
// Boyer-Lindquist coordinates used by the ray tracer.
void cameraFrame(Vec3& pos, Vec3& fwd, Vec3& right, Vec3& up) {
    double st = std::sin(Cam.incl), ct = std::cos(Cam.incl);
    pos = Vec3((float)(Cam.dist * st * std::cos(Cam.azim)),
               (float)(Cam.dist * st * std::sin(Cam.azim)),
               (float)(Cam.dist * ct));
    fwd = normalize(Vec3(0, 0, 0) - pos);
    Vec3 worldUp(0, 0, 1);
    if (std::abs(dot(fwd, worldUp)) > 0.999f) worldUp = Vec3(0, 1, 0);
    right = normalize(cross(fwd, worldUp));
    up = cross(right, fwd);
}

void drawFullscreen() {
    glBindVertexArray(gfx::fullscreenVAO());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// ------------------------------------------------------------- PNG writer ---

void put32(std::vector<unsigned char>& v, unsigned x) {
    v.push_back((x >> 24) & 0xff); v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff);  v.push_back(x & 0xff);
}

unsigned crc32buf(const unsigned char* d, size_t n) {
    static unsigned tab[256];
    static bool init = false;
    if (!init) {
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        init = true;
    }
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = tab[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void chunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data) {
    put32(out, (unsigned)data.size());
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32(out, crc32buf(td.data(), td.size()));
}

// Minimal PNG with stored (uncompressed) deflate blocks -- no zlib needed.
bool writePNG(const char* path, int w, int h, const unsigned char* rgb) {
    std::vector<unsigned char> raw;
    raw.reserve(size_t(h) * (1 + size_t(w) * 3));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const unsigned char* row = rgb + size_t(y) * w * 3;
        raw.insert(raw.end(), row, row + size_t(w) * 3);
    }
    std::vector<unsigned char> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = (pos + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xff); z.push_back((n >> 8) & 0xff);
        z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    unsigned a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<unsigned char> ihdr;
    put32(ihdr, w); put32(ihdr, h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

std::string gShotName;
int  gFrameLimit = 0;
bool gBench = false;

void screenshot(const char* forced = nullptr) {
    std::vector<unsigned char> px(size_t(gFbW) * gFbH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, gFbW, gFbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    // OpenGL rows come out bottom-up.
    std::vector<unsigned char> flip(px.size());
    for (int y = 0; y < gFbH; ++y)
        std::memcpy(&flip[size_t(y) * gFbW * 3], &px[size_t(gFbH - 1 - y) * gFbW * 3], size_t(gFbW) * 3);
    static int n = 0;
    std::string name = forced ? std::string(forced) : fmt("blackhole_%04d.png", n++);
    if (writePNG(name.c_str(), gFbW, gFbH, flip.data()))
        std::printf("saved %s (%dx%d)\n", name.c_str(), gFbW, gFbH);
}

// ------------------------------------------------------------- resources ---

void resizeTargets() {
    int rw = std::max(64, int(gFbW * gRenderScale));
    int rh = std::max(64, int(gFbH * gRenderScale));
    if (rtRay.w != rw || rtRay.h != rh) {
        rtRay.create(rw, rh, GL_RGBA16F);
        rtAccum[0].create(rw, rh, GL_RGBA32F);
        rtAccum[1].create(rw, rh, GL_RGBA32F);
        markDirty();
    }
    if (rtScene.w != gFbW || rtScene.h != gFbH)
        rtScene.create(gFbW, gFbH, GL_RGBA16F, GL_LINEAR, true);

    // Stop the chain while the smallest mip is still large enough to carry
    // shape.  Going all the way down to a handful of texels and then bilinearly
    // stretching it back across the screen paints visible blocky curtains.
    int bw = std::max(1, rw / 2), bh = std::max(1, rh / 2);
    gBloomLevels = 0;
    for (int i = 0; i < kMaxBloom && bw >= 24 && bh >= 24; ++i) {
        if (bloomRT[i].w != bw || bloomRT[i].h != bh)
            bloomRT[i].create(bw, bh, GL_RGBA16F);
        ++gBloomLevels;
        bw = std::max(1, bw / 2); bh = std::max(1, bh / 2);
    }
}

bool loadShaders() {
    std::string log;
    bool ok = true;
    ok &= progBH.load(gShaderDir + "/fullscreen.vert", gShaderDir + "/blackhole.frag", log);
    ok &= progPost.load(gShaderDir + "/fullscreen.vert", gShaderDir + "/post.frag", log);
    ok &= progBloom.load(gShaderDir + "/fullscreen.vert", gShaderDir + "/bloom.frag", log);
    ok &= progAccum.load(gShaderDir + "/fullscreen.vert", gShaderDir + "/accum.frag", log);
    ok &= progMesh.load(gShaderDir + "/mesh.vert", gShaderDir + "/mesh.frag", log);
    gShaderLog = log;
    if (!log.empty()) std::fprintf(stderr, "%s\n", log.c_str());
    return ok;
}

void buildFunnelIfNeeded() {
    if (gFunnelBuilt && std::abs(gBuiltSpin - P.spin) < 1e-9) return;
    geo::buildFunnel(meshFunnel, meshFunnelGrid, meshRings, P.spin, 10.0, 160, 128, &gFunnelClamped);
    geo::uploadTrajectories(meshTraj, geo::buildTestParticles(P.spin, 10.0, 6), 1.0f);
    geo::uploadTrajectories(meshPhotons, geo::buildPhotonPaths(P.spin, 10.0, 13), 0.55f);
    gFunnelBuilt = true;
    gBuiltSpin = P.spin;
}

void buildConesIfNeeded() {
    static double spinBuilt = -99;
    if (gConesBuilt && std::abs(spinBuilt - P.spin) < 1e-9) return;
    geo::buildLightCones(meshCones, meshFloor, meshConeMarks, P.spin, 7.0);
    gConesBuilt = true;
    spinBuilt = P.spin;
}

// ------------------------------------------------------------- rendering ---

void renderRayTraced() {
    Vec3 pos, fwd, right, up;
    cameraFrame(pos, fwd, right, up);

    float jx = 0, jy = 0;
    if (P.accumulate && gAccumCount > 0) {
        // Halton (2,3) sub-pixel offsets.
        auto halton = [](int i, int b) {
            double f = 1.0, r = 0.0;
            while (i > 0) { f /= b; r += f * (i % b); i /= b; }
            return r;
        };
        jx = float(halton(gAccumCount, 2) - 0.5);
        jy = float(halton(gAccumCount, 3) - 0.5);
    }

    rtRay.bind();
    glClear(GL_COLOR_BUFFER_BIT);
    progBH.use();
    progBH.set("uRes", (float)rtRay.w, (float)rtRay.h);
    progBH.set("uSpin", (float)P.spin);
    progBH.set("uCamP", pos.x, pos.y, pos.z);
    progBH.set("uCamF", fwd.x, fwd.y, fwd.z);
    progBH.set("uCamR", right.x, right.y, right.z);
    progBH.set("uCamU", up.x, up.y, up.z);
    progBH.set("uTanHalf", (float)std::tan(Cam.fovY * 0.5));
    progBH.set("uAspect", (float)rtRay.w / (float)rtRay.h);
    progBH.set("uJitter", jx, jy);
    progBH.set("uTimeCoord", (float)gCoordTime);

    progBH.set("uDiskMode", P.diskMode);
    progBH.set("uDiskIn", (float)diskInner());
    progBH.set("uDiskOut", (float)P.diskOuter);
    progBH.set("uDiskBright", 1.0f);
    progBH.set("uTurb", (float)P.turbulence);
    progBH.set("uLUTLogRange", (float)gLutLogRIn, (float)gLutLogROut);
    progBH.set("uTempRef", (float)gTempRef);

    // Colour remap: T_display = A * T_observed^B.
    // Physical mode is the identity.  The remapped mode is a pure linear rescale
    // of temperature (B = 1), i.e. exactly the picture of an identical disk that
    // happens to be cooler by a constant factor -- every ratio, including the
    // Doppler and gravitational shifts, is preserved.
    double tb = 1.0;
    double ta = P.physicalColor ? 1.0 : (11000.0 / std::max(gTempRef, 1.0));
    progBH.set("uTempA", (float)ta);
    progBH.set("uTempB", (float)tb);

    progBH.set("uCoronaH", 0.20f);
    progBH.set("uCoronaOut", (float)std::min(P.diskOuter * 1.6, 70.0));
    progBH.set("uCoronaBright", (float)P.coronaBright);
    progBH.set("uCoronaTemp", (float)(gTempRef * 0.85));

    progBH.set("uBBLogRange", (float)BB_LOG_MIN, (float)BB_LOG_MAX);
    progBH.set("uStarBright", (float)P.starBright);
    progBH.set("uSkyGrid", (float)P.skyGrid);
    progBH.set("uNebula", (float)P.nebula);

    progBH.set("uTol", (float)P.tol);
    progBH.set("uMaxSteps", P.maxSteps);
    progBH.set("uRFar", (float)P.rFar);
    progBH.set("uDebugView", P.debugView);
    progBH.set("uExposure", 1.0f);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texTemp);
    progBH.set("uTempLUT", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texBB);
    progBH.set("uBBLUT", 1);

    drawFullscreen();

    // ---- progressive accumulation ----
    int dst = 1 - gAccumSrc;
    rtAccum[dst].bind();
    progAccum.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rtRay.tex);
    progAccum.set("uNew", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rtAccum[gAccumSrc].tex);
    progAccum.set("uHistory", 1);
    // While the disk is turning we cannot converge to a single image, so the
    // accumulation degrades gracefully into a short temporal filter that still
    // anti-aliases the photon ring and the lensed stars.  Paused or static, the
    // weight keeps falling as 1/n and the image converges properly.
    float wgt;
    if (!P.accumulate || gAccumCount == 0) wgt = 1.0f;
    else if (gAnimating)                   wgt = std::max(1.0f / float(gAccumCount + 1), 0.22f);
    else                                   wgt = 1.0f / float(gAccumCount + 1);
    progAccum.set("uWeight", wgt);
    drawFullscreen();
    gAccumSrc = dst;
    gAccumCount++;
}

Mat4 sceneCamera(Vec3& eye, float centerZ, float distScale, double inclScale) {
    // The 3-D views read best from a three-quarter angle, so the shared orbit
    // inclination is remapped here rather than being taken raw.
    double incl = std::clamp(Cam.incl * inclScale, 0.05, kerr::PI - 0.05);
    double st = std::sin(incl), ct = std::cos(incl);
    Vec3 center(0, 0, centerZ);
    eye = center + Vec3((float)(Cam.dist * distScale * st * std::cos(Cam.azim)),
                        (float)(Cam.dist * distScale * st * std::sin(Cam.azim)),
                        (float)(Cam.dist * distScale * ct));
    Mat4 proj = perspective((float)Cam.fovY, (float)gFbW / (float)gFbH, 0.05f, 4000.0f);
    Mat4 view = lookAt(eye, center, Vec3(0, 0, 1));
    return proj * view;
}

void renderMeshScene() {
    rtScene.bind();
    glClearColor(0.004f, 0.006f, 0.013f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Vec3 eye;
    Mat4 mvp;
    Mat4 model = Mat4::identity();

    progMesh.use();
    progMesh.setMat4("uModel", model.m);

    if (gView == VIEW_FUNNEL) {
        buildFunnelIfNeeded();
        // Frame on the middle of the funnel, whose depth depends on the spin.
        mvp = sceneCamera(eye, (float)(-0.40 * geo::funnelDepth()), 0.50f, 0.86);
        progMesh.setMat4("uMVP", mvp.m);
        progMesh.set("uEye", eye.x, eye.y, eye.z);

        progMesh.set("uLit", 1);
        progMesh.set("uAlpha", 1.0f);
        // Push the shaded surface back in depth so the grid, the marker rings
        // and the geodesics all draw cleanly on top of it from either side.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        meshFunnel.draw();
        glDisable(GL_POLYGON_OFFSET_FILL);

        glDepthMask(GL_FALSE);
        progMesh.set("uLit", 0);
        progMesh.set("uAlpha", 0.9f);
        meshFunnelGrid.draw();
        progMesh.set("uAlpha", 1.6f);
        meshRings.draw();
        // Straight alpha, not additive: over the lit surface an additive curve
        // saturates to white and loses the colour that identifies it.
        progMesh.set("uAlpha", 1.0f);
        meshPhotons.draw();
        meshTraj.draw();
        glDepthMask(GL_TRUE);
    } else {
        buildConesIfNeeded();
        mvp = sceneCamera(eye, 0.7f, 0.30f, 0.72);
        progMesh.setMat4("uMVP", mvp.m);
        progMesh.set("uEye", eye.x, eye.y, eye.z);
        progMesh.set("uLit", 0);
        progMesh.set("uAlpha", 1.0f);
        meshFloor.draw();
        progMesh.set("uAlpha", 1.5f);
        meshConeMarks.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        progMesh.set("uAlpha", 1.1f);
        meshCones.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
}

void runBloom(GLuint srcTex, int srcW, int srcH) {
    progBloom.use();
    progBloom.set("uThreshold", 1.6f);
    progBloom.set("uRadius", 1.0f);

    // Downsample chain
    for (int i = 0; i < gBloomLevels; ++i) {
        bloomRT[i].bind();
        glClear(GL_COLOR_BUFFER_BIT);
        GLuint src = (i == 0) ? srcTex : bloomRT[i - 1].tex;
        int sw = (i == 0) ? srcW : bloomRT[i - 1].w;
        int sh = (i == 0) ? srcH : bloomRT[i - 1].h;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
        progBloom.set("uSrc", 0);
        progBloom.set("uTexel", 1.0f / sw, 1.0f / sh);
        progBloom.set("uMode", i == 0 ? 0 : 1);
        drawFullscreen();
    }
    // Upsample and accumulate
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    progBloom.set("uMode", 2);
    for (int i = gBloomLevels - 1; i > 0; --i) {
        bloomRT[i - 1].bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloomRT[i].tex);
        progBloom.set("uSrc", 0);
        progBloom.set("uTexel", 1.0f / bloomRT[i].w, 1.0f / bloomRT[i].h);
        drawFullscreen();
    }
    glDisable(GL_BLEND);
}

// --------------------------------------------------------------- the HUD ---

void drawHUD(double fps, double ms) {
    text::begin(gFbW, gFbH);

    // The glyph atlas is already rasterised at the display scale, so this is
    // just the size in atlas units -- do not multiply by the DPI ratio again.
    float S = 0.76f;
    float lh = text::lineHeight(S);
    float pad = 14.0f * S / 0.62f;

    double rh = kerr::horizonOuter(P.spin);
    double rIsco = kerr::iscoRadius(P.spin, true);
    double rPh = kerr::photonRadius(P.spin, true);
    double rPhR = kerr::photonRadius(P.spin, false);
    double rg = spectrum::gravRadius(P.massSolar);

    std::vector<std::string> L;
    const char* viewName[] = {"RAY-TRACED KERR", "SPACETIME EMBEDDING", "LIGHT CONES"};
    L.push_back(std::string("== ") + viewName[gView] + " ==");
    L.push_back("");
    L.push_back(fmt("spin  a/M      %.4f", P.spin));
    L.push_back(fmt("mass  M        %.3g Msun   (r_g = %.3g km)", P.massSolar, rg * 1e-5));
    L.push_back(fmt("accr  L/L_Edd  %.3f        eta = %.4f", P.eddRatio, gEta));
    L.push_back(fmt("Mdot           %.3g g/s  = %.3g Msun/yr",
                    gMdot, gMdot * spectrum::YEAR / spectrum::M_SUN));
    L.push_back(fmt("L_disk         %.3g erg/s", gLumErgS));
    L.push_back("");
    L.push_back(fmt("r_+  horizon   %.4f M", rh));
    L.push_back(fmt("r_-  inner     %.4f M", kerr::horizonInner(P.spin)));
    L.push_back(fmt("r_E  ergo(eq)  %.4f M", 2.0));
    L.push_back(fmt("r_ph pro/retro %.4f / %.4f M", rPh, rPhR));
    L.push_back(fmt("r_ISCO         %.4f M", rIsco));
    L.push_back(fmt("disk  r        %.2f - %.2f M", rIsco, P.diskOuter));
    L.push_back(fmt("T_max          %.4g K   (peak %.1f nm)",
                    gTmax, spectrum::wienPeakNm(gTmax)));
    L.push_back("");
    L.push_back(fmt("camera r       %.2f M   incl %.1f deg",
                    Cam.dist, Cam.incl * 180.0 / kerr::PI));
    L.push_back(fmt("fov            %.1f deg", Cam.fovY * 180.0 / kerr::PI));

    if (gView == VIEW_RAYTRACE) {
        const char* dm[] = {"off", "opaque NT disk", "disk + optically-thin halo", "halo only"};
        const char* dv[] = {"beauty", "RK steps", "|H| constraint violation", "redshift g"};
        L.push_back("");
        const char* tm[] = {"asinh (astronomical)", "ACES filmic", "linear"};
        L.push_back(fmt("disk mode      %s", dm[P.diskMode]));
        L.push_back(fmt("colour         %s", P.physicalColor ? "physical Planck"
                                                             : "linear rescale into visible"));
        L.push_back(fmt("tone curve     %s   exposure %.2f", tm[P.tonemap], P.exposure));
        L.push_back(fmt("view           %s", dv[P.debugView]));
        L.push_back(fmt("integrator     Cash-Karp RK4(5), tol %.1e, <= %d steps", P.tol, P.maxSteps));
        L.push_back(fmt("render         %dx%d  (%.0f%%)  samples %d",
                        rtRay.w, rtRay.h, gRenderScale * 100.0, gAccumCount));
    } else if (gView == VIEW_FUNNEL) {
        L.push_back("");
        L.push_back("Isometric embedding of the equatorial plane at fixed t.");
        L.push_back("Distance measured ALONG the surface is proper distance,");
        L.push_back("so the funnel is the extra space the hole adds. The");
        L.push_back("throat is the horizon: the trapdoor at the bottom.");
        L.push_back(fmt("throat depth   %.2f M below the outer rim", geo::funnelDepth()));
        L.push_back("surface colour = log Kretschmann scalar (tidal curvature)");
        if (gFunnelClamped) {
            L.push_back("note: part of this slice is not Euclidean-embeddable");
            L.push_back("      at this spin; the surface is clamped there.");
        }
        L.push_back("");
        L.push_back("green ring = ISCO      yellow = photon orbit");
        L.push_back("orange     = ergosphere  red   = event horizon");
        L.push_back("curves = exact geodesics: photons and test masses");
    } else {
        L.push_back("");
        L.push_back("Future light cones in ingoing Kerr-Schild coordinates,");
        L.push_back("which stay regular across the horizon. Vertical axis is");
        L.push_back("time; each rim is where light gets to in one time step.");
        L.push_back("Every cone is drawn at the same scale.");
        L.push_back("");
        L.push_back("pale stub  = staying still. Inside the cone or not?");
        L.push_back("blue   normal: you can hover at fixed r and phi");
        L.push_back("orange ergoregion: no static observers, must corotate");
        L.push_back("red    inside r_+: every future direction has dr < 0");
        L.push_back("       -- the trapdoor, in causal terms");
    }

    L.push_back("");
    L.push_back(fmt("%.1f fps   %.1f ms", fps, ms));

    float w = 0;
    for (auto& s : L) w = std::max(w, text::width(s, S));
    float boxW = w + pad * 2;
    float boxH = lh * L.size() + pad * 2;
    text::rect(pad * 0.6f, pad * 0.6f, boxW, boxH, 0.02f, 0.03f, 0.06f, 0.72f);

    float y = pad * 0.6f + pad;
    for (size_t i = 0; i < L.size(); ++i) {
        float r = 0.72f, g = 0.85f, b = 1.0f;
        if (i == 0) { r = 1.0f; g = 0.85f; b = 0.45f; }
        text::draw(pad * 0.6f + pad, y, S, r, g, b, 0.96f, L[i]);
        y += lh;
    }

    if (gShowHelp) {
        std::vector<std::string> H = {
            "CONTROLS",
            "drag / scroll   orbit, zoom",
            "1 2 3           ray trace | embedding | light cones",
            "[ ]             spin  a/M",
            "- =             disk outer radius",
            "; '             black hole mass",
            ", .             accretion rate",
            "d               disk mode",
            "c               colour: physical <-> remapped",
            "g               lensing grid on the sky",
            "n               nebula background",
            "t               disk turbulence",
            "v               debug view (steps / |H| / redshift)",
            "9 0             integrator step budget",
            "o p             exposure",
            "b               bloom",
            "m               tone curve: asinh / ACES / linear",
            "space           pause disk rotation",
            "a               progressive accumulation",
            "j k             render resolution",
            "f               save PNG screenshot",
            "r               reload shaders",
            "h               hide this panel",
            "esc             quit",
        };
        float hw = 0;
        for (auto& s : H) hw = std::max(hw, text::width(s, S));
        float bx = gFbW - hw - pad * 3;
        float bh = lh * H.size() + pad * 2;
        text::rect(bx - pad, pad * 0.6f, hw + pad * 2, bh, 0.02f, 0.03f, 0.06f, 0.72f);
        float hy = pad * 0.6f + pad;
        for (size_t i = 0; i < H.size(); ++i) {
            float r = (i == 0) ? 1.0f : 0.62f, g = (i == 0) ? 0.85f : 0.78f, b = (i == 0) ? 0.45f : 0.92f;
            text::draw(bx, hy, S, r, g, b, 0.94f, H[i]);
            hy += lh;
        }
    }

    if (!gShaderLog.empty()) {
        text::rect(pad, gFbH - lh * 3 - pad, gFbW - pad * 2, lh * 2 + pad, 0.25f, 0.02f, 0.02f, 0.85f);
        text::draw(pad * 1.5f, gFbH - lh * 2.4f - pad * 0.5f, S, 1.0f, 0.55f, 0.5f, 1.0f,
                   gShaderLog.substr(0, 160));
    }

    text::end();
}

// ---------------------------------------------------------------- input ----

void onKey(GLFWwindow* w, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    double step = shift ? 0.2 : 1.0;

    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_1: gView = VIEW_RAYTRACE; markDirty(); break;
        case GLFW_KEY_2: gView = VIEW_FUNNEL; break;
        case GLFW_KEY_3: gView = VIEW_CONES; break;

        case GLFW_KEY_LEFT_BRACKET:
            P.spin = std::max(0.0, P.spin - 0.02 * step);
            gFunnelBuilt = gConesBuilt = false; rebuildDiskLUT(); break;
        case GLFW_KEY_RIGHT_BRACKET:
            P.spin = std::min(0.998, P.spin + 0.02 * step);
            gFunnelBuilt = gConesBuilt = false; rebuildDiskLUT(); break;

        case GLFW_KEY_MINUS: P.diskOuter = std::max(diskInner() * 1.4, P.diskOuter - 1.0); rebuildDiskLUT(); break;
        case GLFW_KEY_EQUAL: P.diskOuter = std::min(200.0, P.diskOuter + 1.0); rebuildDiskLUT(); break;

        case GLFW_KEY_SEMICOLON: P.massSolar = std::max(1.0, P.massSolar / 1.6); rebuildDiskLUT(); break;
        case GLFW_KEY_APOSTROPHE: P.massSolar = std::min(1e11, P.massSolar * 1.6); rebuildDiskLUT(); break;

        case GLFW_KEY_COMMA: P.eddRatio = std::max(1e-6, P.eddRatio / 1.35); rebuildDiskLUT(); break;
        case GLFW_KEY_PERIOD: P.eddRatio = std::min(10.0, P.eddRatio * 1.35); rebuildDiskLUT(); break;

        case GLFW_KEY_D: P.diskMode = (P.diskMode + 1) % 4; markDirty(); break;
        case GLFW_KEY_C: P.physicalColor = !P.physicalColor; markDirty(); break;
        case GLFW_KEY_G: P.skyGrid = (P.skyGrid > 0.0) ? 0.0 : 0.9; markDirty(); break;
        case GLFW_KEY_N: P.nebula = (P.nebula > 0.0) ? 0.0 : 1.0; markDirty(); break;
        case GLFW_KEY_T: P.turbulence = (P.turbulence > 0.0) ? 0.0 : 0.55; markDirty(); break;
        case GLFW_KEY_V: P.debugView = (P.debugView + 1) % 4; markDirty(); break;
        case GLFW_KEY_B: P.bloom = (P.bloom > 0.0) ? 0.0 : 0.55; break;
        case GLFW_KEY_A: P.accumulate = !P.accumulate; markDirty(); break;
        case GLFW_KEY_SPACE: P.paused = !P.paused; break;

        case GLFW_KEY_9: P.maxSteps = std::max(120, P.maxSteps - 100); markDirty(); break;
        case GLFW_KEY_0: P.maxSteps = std::min(6000, P.maxSteps + 100); markDirty(); break;

        case GLFW_KEY_O: P.exposure = std::max(0.02, P.exposure / 1.2); break;
        case GLFW_KEY_P: P.exposure = std::min(200.0, P.exposure * 1.2); break;

        case GLFW_KEY_J: gRenderScale = std::max(0.25, gRenderScale - 0.125); resizeTargets(); break;
        case GLFW_KEY_K: gRenderScale = std::min(1.0, gRenderScale + 0.125); resizeTargets(); break;

        case GLFW_KEY_M: P.tonemap = (P.tonemap + 1) % 3; break;
        case GLFW_KEY_F: screenshot(); break;
        case GLFW_KEY_R: loadShaders(); markDirty(); break;
        case GLFW_KEY_H: gShowHelp = !gShowHelp; break;
        case GLFW_KEY_U: gShowHud = !gShowHud; break;
        default: break;
    }
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gDragging = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &gLastX, &gLastY);
    }
}

void onCursor(GLFWwindow*, double x, double y) {
    if (!gDragging) return;
    double dx = x - gLastX, dy = y - gLastY;
    gLastX = x; gLastY = y;
    Cam.azim -= dx * 0.005;
    Cam.incl = std::clamp(Cam.incl + dy * 0.005, 0.02, kerr::PI - 0.02);
    markDirty();
}

void onScroll(GLFWwindow*, double, double dy) {
    Cam.dist = std::clamp(Cam.dist * std::exp(-dy * 0.08), 1.6, 4000.0);
    markDirty();
}

void onResize(GLFWwindow* w, int, int) {
    glfwGetFramebufferSize(w, &gFbW, &gFbH);
    glfwGetWindowSize(w, &gWinW, &gWinH);
    resizeTargets();
    markDirty();
}

}  // namespace

// ============================================================================

int runApp(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](double d) { return (i + 1 < argc) ? atof(argv[++i]) : d; };
        if (a == "--spin") P.spin = std::clamp(next(P.spin), 0.0, 0.998);
        else if (a == "--mass") P.massSolar = next(P.massSolar);
        else if (a == "--edd") P.eddRatio = next(P.eddRatio);
        else if (a == "--incl") Cam.incl = next(80.0) * kerr::PI / 180.0;
        else if (a == "--dist") Cam.dist = next(Cam.dist);
        else if (a == "--disk-outer") P.diskOuter = next(P.diskOuter);
        else if (a == "--steps") P.maxSteps = (int)next(P.maxSteps);
        else if (a == "--scale") gRenderScale = std::clamp(next(gRenderScale), 0.2, 2.0);
        else if (a == "--width") gWinW = (int)next(gWinW);
        else if (a == "--height") gWinH = (int)next(gWinH);
        else if (a == "--view") gView = (View)std::clamp((int)next(0), 0, 2);
        else if (a == "--turb") P.turbulence = next(P.turbulence);
        else if (a == "--exposure") P.exposure = next(P.exposure);
        else if (a == "--disk-mode") P.diskMode = std::clamp((int)next(2), 0, 3);
        else if (a == "--grid") P.skyGrid = next(0.9);
        else if (a == "--debug") P.debugView = std::clamp((int)next(0), 0, 3);
        else if (a == "--tonemap") P.tonemap = std::clamp((int)next(0), 0, 2);
        else if (a == "--bloom") P.bloom = next(0.0);
        else if (a == "--nebula") P.nebula = next(0.0);
        else if (a == "--stars") P.starBright = next(0.0);
        else if (a == "--corona") P.coronaBright = next(0.0);
        else if (a == "--no-hud") gShowHud = false;
        else if (a == "--no-help") gShowHelp = false;
        else if (a == "--frames") gFrameLimit = (int)next(0);
        else if (a == "--bench") { gBench = true; gFrameLimit = (int)next(300); }
        else if (a == "--shot" && i + 1 < argc) gShotName = argv[++i];
    }

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWwindow* win = glfwCreateWindow(gWinW, gWinH, "Kerr black hole — GR ray tracer", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(gBench ? 0 : 1);

    glfwSetKeyCallback(win, onKey);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);
    glfwSetFramebufferSizeCallback(win, onResize);
    glfwGetFramebufferSize(win, &gFbW, &gFbH);

    std::printf("GL %s | %s | GLSL %s\n", glGetString(GL_VERSION),
                glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));

    if (!loadShaders()) { std::fprintf(stderr, "shader load failed\n"); return 1; }
    text::init(15.0f * std::max(1.0f, (float)gFbW / (float)gWinW));

    texBB = gfx::makeLUT1D(spectrum::blackbodyLUT(BB_N, BB_LOG_MIN, BB_LOG_MAX), BB_N);
    rebuildDiskLUT();
    resizeTargets();

    double prev = glfwGetTime();
    double fpsAvg = 60.0, msAvg = 16.0;
    double reloadTimer = 0;
    int frameNo = 0;
    double benchStart = 0; int benchFrames = 0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        double dt = std::min(now - prev, 0.1);
        prev = now;

        glfwPollEvents();

        reloadTimer += dt;
        if (reloadTimer > 0.5) {
            reloadTimer = 0;
            std::string log;
            bool changed = false;
            changed |= progBH.reloadIfChanged(log);
            changed |= progPost.reloadIfChanged(log);
            changed |= progBloom.reloadIfChanged(log);
            changed |= progAccum.reloadIfChanged(log);
            changed |= progMesh.reloadIfChanged(log);
            if (changed) { gShaderLog = log; markDirty(); }
        }

        gAnimating = !P.paused && P.turbulence > 0.0 && P.diskMode != 0;
        if (!P.paused) gCoordTime += dt * 12.0 * P.timeRate;

        GLuint hdrTex = 0;
        int hdrW = 0, hdrH = 0;

        if (gView == VIEW_RAYTRACE) {
            if (!P.accumulate) gAccumCount = 0;
            renderRayTraced();
            hdrTex = rtAccum[gAccumSrc].tex;
            hdrW = rtAccum[gAccumSrc].w;
            hdrH = rtAccum[gAccumSrc].h;
        } else {
            renderMeshScene();
            hdrTex = rtScene.tex;
            hdrW = rtScene.w;
            hdrH = rtScene.h;
        }

        if (P.bloom > 0.0) runBloom(hdrTex, hdrW, hdrH);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, gFbW, gFbH);
        glClear(GL_COLOR_BUFFER_BIT);
        progPost.use();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrTex);
        progPost.set("uHDR", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, P.bloom > 0.0 ? bloomRT[0].tex : hdrTex);
        progPost.set("uBloom", 1);
        progPost.set("uBloomStrength", (float)P.bloom);
        progPost.set("uExposure", (float)P.exposure);
        // The 3-D views are line art, not an HDR photograph: an asinh stretch
        // would lift the black background to grey.
        bool mesh = (gView != VIEW_RAYTRACE);
        progPost.set("uTonemap", mesh ? 1 : (P.debugView == 0 ? P.tonemap : 2));
        progPost.set("uAsinh", (float)P.asinhK);
        progPost.set("uExposure", (float)(mesh ? 1.0 : P.exposure));
        progPost.set("uBloomStrength", (float)(mesh ? P.bloom * 0.5 : P.bloom));
        progPost.set("uVignette", mesh ? 0.15f : 0.30f);
        drawFullscreen();

        double ms = dt * 1000.0;          // whole frame period, not just submit time
        msAvg = msAvg * 0.92 + ms * 0.08;
        fpsAvg = fpsAvg * 0.92 + (1.0 / std::max(dt, 1e-4)) * 0.08;
        if (gShowHud) drawHUD(fpsAvg, msAvg);

        if (gBench) {
            glFinish();
            if (frameNo == 30) benchStart = glfwGetTime();   // skip warm-up
            if (frameNo > 30) benchFrames++;
        }
        bool lastFrame = (gFrameLimit > 0 && ++frameNo >= gFrameLimit);
        if (lastFrame && !gShotName.empty()) {
            glFinish();
            screenshot(gShotName.c_str());
        }

        glfwSwapBuffers(win);
        if (lastFrame) break;
    }

    if (gBench && benchFrames > 0) {
        double el = glfwGetTime() - benchStart;
        std::printf("bench: %d frames in %.3f s  ->  %.1f fps  (%.2f ms/frame)\n"
                    "       %dx%d internal (%.0f%% of %dx%d), <= %d steps/ray, tol %.1e\n",
                    benchFrames, el, benchFrames / el, 1000.0 * el / benchFrames,
                    rtRay.w, rtRay.h, gRenderScale * 100.0, gFbW, gFbH, P.maxSteps, P.tol);
    }
    text::shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
