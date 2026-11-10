// Hud.h — builds the HUD as one dynamic vertex buffer (positions + rgba).
// GL calls live in main.cpp; this file is pure CPU-side batching.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Font5x7.h"

namespace bhlib {

struct HudBatch {
    struct Color { float r, g, b, a; };
    std::vector<float> verts; // per vertex: x, y, r, g, b, a

    void clear() { verts.clear(); }

    void addQuad(float x, float y, float w, float h, Color c) {
        float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
        pushVert(x0, y0, c); pushVert(x1, y0, c); pushVert(x0, y1, c);
        pushVert(x1, y0, c); pushVert(x1, y1, c); pushVert(x0, y1, c);
    }
    void pushVert(float x, float y, Color c) {
        verts.push_back(x); verts.push_back(y);
        verts.push_back(c.r); verts.push_back(c.g); verts.push_back(c.b); verts.push_back(c.a);
    }

    float drawText(float x, float y, float scale, const std::string& text, Color c) {
        float cursor = x;
        const float advance = 6.0f * scale;
        for (char ch : text) {
            if (ch == '\n') break;
            const Glyph* g = glyphFor(ch);
            for (int row = 0; row < 7; ++row) {
                uint8_t bits = g->row[row];
                for (int col = 0; col < 5; ++col)
                    if (bits & (0x10 >> col))
                        addQuad(cursor + col * scale, y + row * scale, scale, scale, c);
            }
            cursor += advance;
        }
        return cursor;
    }

    static float textWidth(const std::string& text, float scale) {
        return float(text.size()) * 6.0f * scale;
    }

    void addRect(float x, float y, float w, float h, Color c) { addQuad(x, y, w, h, c); }

    void addRectOutline(float x, float y, float w, float h, Color c, float thickness = 1.0f) {
        addRect(x, y, w, thickness, c);
        addRect(x, y + h - thickness, w, thickness, c);
        addRect(x, y, thickness, h, c);
        addRect(x + w - thickness, y, thickness, h, c);
    }
};

} // namespace bhlib
