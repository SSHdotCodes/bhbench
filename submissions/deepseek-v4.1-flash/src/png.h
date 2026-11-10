// Minimal dependency-free PNG writer (8-bit RGB/RGBA, stored deflate blocks).
#pragma once

#include <cstdint>

namespace png {

// channels must be 3 (RGB) or 4 (RGBA). Returns false on I/O failure.
bool write(const char* path, int width, int height, int channels,
           const uint8_t* pixels);

}  // namespace png
