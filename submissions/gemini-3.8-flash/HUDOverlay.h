#ifndef HUD_OVERLAY_H
#define HUD_OVERLAY_H

#include <vector>
#include <string>
#include <cstdint>

class HUDOverlay {
public:
    HUDOverlay();
    ~HUDOverlay();

    void resize(int width, int height);
    void clear();
    void drawText(int x, int y, const std::string& text, uint32_t color = 0xFFFFFFFF, int scale = 1);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void drawRect(int x, int y, int w, int h, uint32_t color);

    const uint32_t* getPixelBuffer() const { return pixels.data(); }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    void drawChar(int x, int y, char c, uint32_t color, int scale);

    int width;
    int height;
    std::vector<uint32_t> pixels;
};

#endif // HUD_OVERLAY_H
