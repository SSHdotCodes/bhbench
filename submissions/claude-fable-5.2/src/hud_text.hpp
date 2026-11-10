// hud_text.hpp — text panel rasteriser for the on-screen read-out.
#pragma once
#include <string>
#include <vector>

namespace hud {

enum class Style { Normal = 0, Title = 1, Dim = 2, Accent = 3 };

struct Line {
    std::string text;
    Style style = Style::Normal;
};

struct Bitmap {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;      // premultiplied alpha, top row first
};

// Renders the lines into a bitmap.  `scale` is the display's pixel density,
// `backing` the opacity of the dark panel behind the text.
// Returns false if text rendering is unavailable on this platform.
bool render(const std::vector<Line>& lines, float pointSize, float scale, float backing, Bitmap& out);

}  // namespace hud
