// Tiny image writers: PNG (zlib) with a PPM fallback.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bh {

// rgb: tightly packed 8-bit RGB, top-down row order (row 0 = top of image).
bool writeImage(const std::string& path, int w, int h, const uint8_t* rgb);

}  // namespace bh
