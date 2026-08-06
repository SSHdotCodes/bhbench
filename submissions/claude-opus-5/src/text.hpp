// text.hpp — HUD text and panels.  The glyph atlas is rasterised at startup
// with CoreText, so the overlay is crisp at any DPI without shipping a font.

#pragma once
#include <string>

namespace text {

bool init(float pixelSize);
void shutdown();

void begin(int screenW, int screenH);
void rect(float x, float y, float w, float h, float r, float g, float b, float a);
void draw(float x, float y, float scale, float r, float g, float b, float a, const std::string& s);
void end();

float lineHeight(float scale);
float width(const std::string& s, float scale);

}  // namespace text
