#include "image_io.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if BH_USE_ZLIB
#include <zlib.h>
#endif

namespace bh {
namespace {

bool writePPM(const std::string& path, int w, int h, const uint8_t* rgb) {
    std::string p = path;
    if (p.size() < 4 || p.substr(p.size() - 4) != ".ppm") {
        p = path.substr(0, path.find_last_of('.')) + ".ppm";
    }
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, size_t(w) * size_t(h) * 3, f);
    std::fclose(f);
    std::printf("[image] wrote %s (%dx%d, PPM)\n", p.c_str(), w, h);
    return true;
}

#if BH_USE_ZLIB
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t((x >> 24) & 0xff));
    v.push_back(uint8_t((x >> 16) & 0xff));
    v.push_back(uint8_t((x >> 8) & 0xff));
    v.push_back(uint8_t(x & 0xff));
}

void chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t n) {
    put32(out, uint32_t(n));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), data, data + n);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + start, uInt(4 + n));
    put32(out, uint32_t(crc));
}

bool writePNG(const std::string& path, int w, int h, const uint8_t* rgb) {
    // Raw scanlines with filter byte 0.
    size_t stride = size_t(w) * 3;
    std::vector<uint8_t> raw((stride + 1) * size_t(h));
    for (int y = 0; y < h; ++y) {
        raw[size_t(y) * (stride + 1)] = 0;
        std::memcpy(raw.data() + size_t(y) * (stride + 1) + 1, rgb + size_t(y) * stride, stride);
    }
    uLongf zlen = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    z.resize(zlen);

    std::vector<uint8_t> out;
    out.reserve(z.size() + 128);
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.insert(out.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0] = uint8_t((w >> 24) & 0xff); ihdr[1] = uint8_t((w >> 16) & 0xff);
    ihdr[2] = uint8_t((w >> 8) & 0xff);  ihdr[3] = uint8_t(w & 0xff);
    ihdr[4] = uint8_t((h >> 24) & 0xff); ihdr[5] = uint8_t((h >> 16) & 0xff);
    ihdr[6] = uint8_t((h >> 8) & 0xff);  ihdr[7] = uint8_t(h & 0xff);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // colour type: truecolour RGB
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(out, "IHDR", ihdr, 13);
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    std::printf("[image] wrote %s (%dx%d, PNG)\n", path.c_str(), w, h);
    return true;
}
#endif

}  // namespace

bool writeImage(const std::string& path, int w, int h, const uint8_t* rgb) {
#if BH_USE_ZLIB
    if (path.size() > 4 && path.substr(path.size() - 4) == ".png") {
        if (writePNG(path, w, h, rgb)) return true;
        std::fprintf(stderr, "[image] PNG write failed, falling back to PPM\n");
    }
#endif
    return writePPM(path, w, h, rgb);
}

}  // namespace bh
