#pragma once

#include <cstdint>
#include <vector>

#include "glutil.h"
#include "gridscene.h"
#include "params.h"

class Renderer {
public:
    void init();
    void shutdown();

    // Recreates render targets for a window of the given size.
    void resize(int windowW, int windowH, float renderScale);

    // Renders the scene into the internal final LDR target.
    void renderScene(const Params& p, float time, bool resetAccum, int accumIndex);

    // Blits the final target to the default framebuffer.
    void present();

    // Draws the HUD into the final target (so stills include it).
    void drawHud(const Params& p, float time, float fps, int windowW, int windowH);

    // Reads the final LDR target as RGB8.
    bool readFinal(std::vector<uint8_t>& rgb, int& w, int& h);

    int renderWidth() const { return renderW_; }
    int renderHeight() const { return renderH_; }

private:
    void renderLensed(const Params& p, float time, int vx, int vy, int vw, int vh,
                      bool resetAccum, int accumIndex);
    void renderSpacetime(const Params& p, float time, int vx, int vy, int vw, int vh);
    void runBloom();
    void composite(const Params& p, float time);

    GLuint rayProgram_ = 0;
    GLuint bloomDownProgram_ = 0;
    GLuint bloomUpProgram_ = 0;
    GLuint compositeProgram_ = 0;
    GLuint blitProgram_ = 0;
    GLuint fsVAO_ = 0;
    GLuint bbTex_ = 0;

    glu::Framebuffer scene_;      // HDR + depth
    glu::Framebuffer accum_;      // HDR temporal accumulation
    glu::Framebuffer bloom_[4];
    glu::Framebuffer final_;      // LDR

    GridScene grid_;

    int windowW_ = 1, windowH_ = 1;
    int renderW_ = 1, renderH_ = 1;
    int accumCount_ = 0;
};
