// Minimal PNG writer using the system ImageIO framework (no third-party code).
#ifndef BH_PNG_OUT_H
#define BH_PNG_OUT_H

#include <cstdint>

// Writes an 8-bit RGB image. `rgb` holds width*height*3 bytes, row-major from the top.
bool write_png_rgb8(const char* path, const uint8_t* rgb, int width, int height);

#endif
