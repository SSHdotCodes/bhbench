#include "renderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "blackbody.h"
#include "camera.h"
#include "physics.h"
#include "shaders_embedded.h"
#include "text.h"

using m3::Mat4;
using m3::Vec3;

namespace {

float fractf(float x) { return x - std::floor(x); }

void drawFullscreen(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

GLint loc(GLuint prog, const char* name) { return glGetUniformLocation(prog, name); }

}  // namespace

void Renderer::init() {
    rayProgram_ = glu::makeProgram(SHADER_FULLSCREEN_VERT, SHADER_RAYTRACE_FRAG, "raytrace");
    bloomDownProgram_ = glu::makeProgram(SHADER_FULLSCREEN_VERT, SHADER_BLOOM_DOWN_FRAG, "bloom_down");
    bloomUpProgram_ = glu::makeProgram(SHADER_FULLSCREEN_VERT, SHADER_BLOOM_UP_FRAG, "bloom_up");
    compositeProgram_ = glu::makeProgram(SHADER_FULLSCREEN_VERT, SHADER_COMPOSITE_FRAG, "composite");
    blitProgram_ = glu::makeProgram(SHADER_FULLSCREEN_VERT, SHADER_BLIT_FRAG, "blit");

    fsVAO_ = glu::makeFullscreenVAO();

    // Blackbody LUT.
    const int lutN = 1024;
    std::vector<float> lut = bb::buildLUT(lutN, bb::kLutMinT, bb::kLutMaxT);
    glGenTextures(1, &bbTex_);
    glBindTexture(GL_TEXTURE_1D, bbTex_);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB16F, lutN, 0, GL_RGB, GL_FLOAT, lut.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    grid_.init();
}

void Renderer::shutdown() {
    grid_.shutdown();
    scene_.destroy();
    accum_.destroy();
    for (auto& b : bloom_) b.destroy();
    final_.destroy();
    if (bbTex_) glDeleteTextures(1, &bbTex_);
    if (fsVAO_) glDeleteVertexArrays(1, &fsVAO_);
    glDeleteProgram(rayProgram_);
    glDeleteProgram(bloomDownProgram_);
    glDeleteProgram(bloomUpProgram_);
    glDeleteProgram(compositeProgram_);
    glDeleteProgram(blitProgram_);
}

void Renderer::resize(int windowW, int windowH, float renderScale) {
    windowW_ = std::max(1, windowW);
    windowH_ = std::max(1, windowH);
    int rw = std::max(64, int(windowW_ * renderScale));
    int rh = std::max(64, int(windowH_ * renderScale));
    if (rw == renderW_ && rh == renderH_ && final_.fbo != 0) return;

    renderW_ = rw;
    renderH_ = rh;

    scene_.create(renderW_, renderH_, true, true);
    accum_.create(renderW_, renderH_, false, true);
    final_.create(windowW_, windowH_, false, false);

    int w = renderW_, h = renderH_;
    for (int i = 0; i < 4; ++i) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloom_[i].create(w, h, false, true);
    }
    accumCount_ = 0;
}

void Renderer::renderLensed(const Params& p, float time, int vx, int vy, int vw, int vh,
                            bool resetAccum, int frameIndex) {
    OrbitCamera cam;
    cam.yaw = p.yaw;
    cam.pitch = p.pitch;
    cam.distance = p.distance;
    cam.fovDeg = p.fovDeg;

    Vec3 pos = cam.position();
    Vec3 right, up, fwd;
    cam.basis(right, up, fwd);

    float jx = fractf(0.5f + frameIndex * 0.7548776662f) - 0.5f;
    float jy = fractf(0.5f + frameIndex * 0.5698402910f) - 0.5f;

    scene_.bind();
    glViewport(vx, vy, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(rayProgram_);
    glUniform2f(loc(rayProgram_, "uRes"), float(vw), float(vh));
    glUniform2f(loc(rayProgram_, "uViewportOffset"), float(vx), float(vy));
    glUniform2f(loc(rayProgram_, "uJitter"), jx, jy);
    glUniform3f(loc(rayProgram_, "uCamPos"), pos.x, pos.y, pos.z);
    glUniform3f(loc(rayProgram_, "uCamRight"), right.x, right.y, right.z);
    glUniform3f(loc(rayProgram_, "uCamUp"), up.x, up.y, up.z);
    glUniform3f(loc(rayProgram_, "uCamFwd"), fwd.x, fwd.y, fwd.z);
    glUniform1f(loc(rayProgram_, "uTanHalfFov"),
                std::tan(m3::radians(p.fovDeg) * 0.5f));
    glUniform1f(loc(rayProgram_, "uTime"), time);

    glUniform1f(loc(rayProgram_, "uDiskInner"), p.diskInner);
    glUniform1f(loc(rayProgram_, "uDiskOuter"), p.diskOuter);
    glUniform1f(loc(rayProgram_, "uDiskTempPeak"), p.diskTempPeak);
    glUniform1f(loc(rayProgram_, "uDiskBrightness"), p.diskBrightness);
    glUniform1i(loc(rayProgram_, "uPhysical"), p.physical ? 1 : 0);
    glUniform1i(loc(rayProgram_, "uDiskTexture"), p.diskTexture ? 1 : 0);
    glUniform1f(loc(rayProgram_, "uCompress"), p.diskCompress);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_1D, bbTex_);
    glUniform1i(loc(rayProgram_, "uBB"), 0);
    glUniform1f(loc(rayProgram_, "uBBMinT"), float(bb::kLutMinT));
    glUniform1f(loc(rayProgram_, "uBBMaxT"), float(bb::kLutMaxT));

    glUniform1i(loc(rayProgram_, "uShowStars"), p.showStars ? 1 : 0);
    glUniform1f(loc(rayProgram_, "uStarBrightness"), p.starBrightness);

    glUniform1i(loc(rayProgram_, "uShowGrid"), p.showGrid ? 1 : 0);
    glUniform1f(loc(rayProgram_, "uGridOuter"), p.gridOuter);
    glUniform1f(loc(rayProgram_, "uGridIntensity"), p.gridIntensity);

    glUniform1i(loc(rayProgram_, "uMaxSteps"), p.maxSteps);

    drawFullscreen(fsVAO_);

    // Resolve into the accumulation buffer.
    float w = 1.0f;
    if (resetAccum) {
        accumCount_ = 1;
        w = 1.0f;
    } else {
        accumCount_++;
        w = 1.0f / float(accumCount_);
    }

    accum_.bind();
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(blitProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_.color);
    glUniform1i(loc(blitProgram_, "uSrc"), 0);
    glUniform1f(loc(blitProgram_, "uWeight"), w);
    drawFullscreen(fsVAO_);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::renderSpacetime(const Params& p, float time, int vx, int vy, int vw, int vh) {
    scene_.bind();
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClearColor(0.004f, 0.006f, 0.013f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    OrbitCamera cam;
    cam.yaw = p.gridYaw;
    cam.pitch = p.gridPitch;
    cam.distance = p.gridDistance;
    cam.target = Vec3(0.0f, 0.0f, 4.0f);

    Mat4 view = m3::lookAt(cam.position(), cam.target, Vec3(0, 0, 1));
    Mat4 proj = m3::perspective(m3::radians(50.0f), float(vw) / float(vh), 0.5f, 800.0f);
    Mat4 vp = proj * view;

    grid_.draw(vp, time, 1.0f, p.gridZScale);

    // Copy into the accumulation buffer so the composite has a single source.
    accum_.bind();
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glDisable(GL_BLEND);
    glUseProgram(blitProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_.color);
    glUniform1i(loc(blitProgram_, "uSrc"), 0);
    glUniform1f(loc(blitProgram_, "uWeight"), 1.0f);
    drawFullscreen(fsVAO_);
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::runBloom() {
    // Downsample chain.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(bloomDownProgram_);
    glUniform1i(loc(bloomDownProgram_, "uSrc"), 0);
    glUniform1f(loc(bloomDownProgram_, "uThreshold"), 0.75f);
    glUniform1f(loc(bloomDownProgram_, "uKnee"), 0.35f);

    GLuint src = accum_.color;
    int sw = renderW_, sh = renderH_;
    for (int i = 0; i < 4; ++i) {
        bloom_[i].bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform2f(loc(bloomDownProgram_, "uTexel"), 1.0f / float(sw), 1.0f / float(sh));
        glUniform1i(loc(bloomDownProgram_, "uPrefilter"), i == 0 ? 1 : 0);
        drawFullscreen(fsVAO_);
        src = bloom_[i].color;
        sw = bloom_[i].width;
        sh = bloom_[i].height;
    }

    // Upsample chain with additive blending.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(bloomUpProgram_);
    glUniform1i(loc(bloomUpProgram_, "uSrc"), 0);
    glUniform1f(loc(bloomUpProgram_, "uRadius"), 1.0f);
    for (int i = 2; i >= 0; --i) {
        bloom_[i].bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloom_[i + 1].color);
        glUniform2f(loc(bloomUpProgram_, "uTexel"),
                    1.0f / float(bloom_[i + 1].width),
                    1.0f / float(bloom_[i + 1].height));
        drawFullscreen(fsVAO_);
    }
    glDisable(GL_BLEND);
}

void Renderer::composite(const Params& p, float time) {
    final_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(compositeProgram_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accum_.color);
    glUniform1i(loc(compositeProgram_, "uHDR"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_[0].color);
    glUniform1i(loc(compositeProgram_, "uBloom"), 1);

    glUniform1f(loc(compositeProgram_, "uBloomStrength"), p.bloom);
    glUniform1f(loc(compositeProgram_, "uExposure"), p.exposure);
    glUniform1f(loc(compositeProgram_, "uGamma"), p.gamma);
    glUniform1f(loc(compositeProgram_, "uVignette"), p.vignette);
    glUniform1f(loc(compositeProgram_, "uTime"), time);
    glUniform2f(loc(compositeProgram_, "uRes"), float(final_.width), float(final_.height));

    drawFullscreen(fsVAO_);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::renderScene(const Params& p, float time, bool resetAccum, int frameIndex) {
    if (p.mode == 0) {
        renderLensed(p, time, 0, 0, renderW_, renderH_, resetAccum, frameIndex);
    } else if (p.mode == 1) {
        renderSpacetime(p, time, 0, 0, renderW_, renderH_);
    } else {
        int halfW = renderW_ / 2;
        renderLensed(p, time, 0, 0, halfW, renderH_, resetAccum, frameIndex);
        renderSpacetime(p, time, halfW, 0, renderW_ - halfW, renderH_);
    }
    runBloom();
    composite(p, time);
}

void Renderer::present() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, final_.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, final_.width, final_.height,
                      0, 0, windowW_, windowH_,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::drawHud(const Params& p, float time, float fps, int windowW, int windowH) {
    static text::Renderer tr;
    static bool inited = false;
    if (!inited) { tr.init(); inited = true; }

    final_.bind();
    glViewport(0, 0, final_.width, final_.height);
    tr.begin(final_.width, final_.height);

    const float s = 2.0f;
    const float lh = 8.0f * s;
    float x = 18.0f, y = 16.0f;

    auto shadowed = [&](float px, float py, float sc, const std::string& str,
                        float r, float g, float b) {
        tr.draw(px + 1.5f, py + 1.5f, sc, str, 0.0f, 0.0f, 0.0f, 0.85f);
        tr.draw(px, py, sc, str, r, g, b, 1.0f);
    };

    char buf[256];

    shadowed(x, y, 3.0f, "SCHWARZSCHILD BLACK HOLE", 0.85f, 0.93f, 1.0f);
    y += 8.0f * 3.0f + 6.0f;

    const char* modeName = (p.mode == 0) ? "LENSED  (RAY-TRACED GEODESICS)"
                          : (p.mode == 1) ? "SPACETIME CURVATURE (FLAMM PARABOLOID)"
                                          : "SPLIT VIEW";
    shadowed(x, y, s, modeName, 0.55f, 0.85f, 1.0f);
    y += lh + 2.0f;

    snprintf(buf, sizeof(buf), "FPS %5.1f    RENDER %dx%d (%.0f%%)", fps,
             renderW_, renderH_, p.renderScale * 100.0f);
    shadowed(x, y, s, buf, 0.62f, 0.68f, 0.75f);
    y += lh + 2.0f;

    snprintf(buf, sizeof(buf), "CAMERA r = %.1f M   INCLINATION %.1f DEG",
             p.distance, p.pitch * 180.0f / m3::PI);
    shadowed(x, y, s, buf, 0.62f, 0.68f, 0.75f);
    y += lh + 2.0f;

    snprintf(buf, sizeof(buf), "DISK r_in = %.1f M (ISCO)   r_out = %.1f M",
             p.diskInner, p.diskOuter);
    shadowed(x, y, s, buf, 0.62f, 0.68f, 0.75f);
    y += lh + 2.0f;

    snprintf(buf, sizeof(buf), "T_peak = %.0f K   %s", p.diskTempPeak,
             p.physical ? "ABSOLUTE RADIANCE" : "NORMALISED FLUX");
    shadowed(x, y, s, buf, 0.62f, 0.68f, 0.75f);
    y += lh + 2.0f;

    double physT = bh::diskPeakTemperature(p.massSolar, p.eddington);
    snprintf(buf, sizeof(buf), "PHYSICAL: M = %.0e Msun, %.2f Edd -> T_peak = %.0f K",
             p.massSolar, p.eddington, physT);
    shadowed(x, y, s, buf, 0.45f, 0.55f, 0.62f);
    y += lh + 2.0f;

    snprintf(buf, sizeof(buf), "SHADOW b_c = 3 sqrt(3) M = %.3f M   PHOTON SPHERE 3 M",
             bh::kBCrit);
    shadowed(x, y, s, buf, 0.45f, 0.55f, 0.62f);
    y += lh + 2.0f;

    // Controls, bottom-left.
    float cy = float(final_.height) - 7.0f * lh - 18.0f;
    shadowed(x, cy, s, "1 LENSED   2 SPACETIME   3 SPLIT", 0.50f, 0.58f, 0.66f);
    cy += lh;
    shadowed(x, cy, s, "DRAG ORBIT   SCROLL ZOOM   SPACE PAUSE", 0.50f, 0.58f, 0.66f);
    cy += lh;
    shadowed(x, cy, s, "G GRID   N TEXTURE   V PHYSICAL   B BLOOM", 0.50f, 0.58f, 0.66f);
    cy += lh;
    shadowed(x, cy, s, "[ ] EXPOSURE   - = TEMPERATURE   H HIDE HUD", 0.50f, 0.58f, 0.66f);
    cy += lh;
    shadowed(x, cy, s, "R RESET   F FULLSCREEN   ESC QUIT", 0.50f, 0.58f, 0.66f);

    tr.end();
}

bool Renderer::readFinal(std::vector<uint8_t>& rgb, int& w, int& h) {
    w = final_.width;
    h = final_.height;
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, final_.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    rgb.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = rgba.data() + static_cast<size_t>(y) * w * 4;
        uint8_t* dst = rgb.data() + static_cast<size_t>(h - 1 - y) * w * 3;
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return true;
}
