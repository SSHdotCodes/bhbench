#pragma once
// Minimal dependency-free PNG writer (RGB8, zlib "stored" blocks).
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>

namespace pngw {

static uint32_t crc_table[256];
static bool crc_ready = false;
static void init_crc() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = true;
}
static uint32_t crc32(const uint8_t* d, size_t n) {
    if (!crc_ready) init_crc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = crc_table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t adler32(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}
static void chunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t n) {
    be32(out, (uint32_t)n);
    std::vector<uint8_t> body((const uint8_t*)type, (const uint8_t*)type + 4);
    body.insert(body.end(), data, data + n);
    out.insert(out.end(), body.begin(), body.end());
    be32(out, crc32(body.data(), body.size()));
}
static void zlib_store(std::vector<uint8_t>& out, const uint8_t* d, size_t n) {
    out.push_back(0x78); out.push_back(0x01);
    size_t i = 0;
    while (i < n) {
        size_t len = n - i; if (len > 65535) len = 65535;
        bool last = (i + len) >= n;
        out.push_back(last ? 1 : 0);
        out.push_back(len & 0xFF); out.push_back((len >> 8) & 0xFF);
        out.push_back(~len & 0xFF); out.push_back((~len >> 8) & 0xFF);
        out.insert(out.end(), d + i, d + i + len);
        i += len;
    }
    uint32_t ad = adler32(d, n);
    out.push_back((ad >> 24) & 0xFF); out.push_back((ad >> 16) & 0xFF);
    out.push_back((ad >> 8) & 0xFF);  out.push_back(ad & 0xFF);
}

// rgb: w*h*3 bytes, bottom-up like OpenGL glReadPixels
inline bool writeRGB(const char* path, int w, int h, const uint8_t* rgbBottomUp) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = h - 1; y >= 0; --y) {           // flip to top-down
        raw.push_back(0);                        // filter: none
        raw.insert(raw.end(), rgbBottomUp + (size_t)y * w * 3,
                   rgbBottomUp + (size_t)(y + 1) * w * 3);
    }
    std::vector<uint8_t> z;
    zlib_store(z, raw.data(), raw.size());

    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(2);        // 8-bit, truecolor
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);

    std::vector<uint8_t> out;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);
    chunk(out, "IHDR", ihdr.data(), ihdr.size());
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}

} // namespace pngw
