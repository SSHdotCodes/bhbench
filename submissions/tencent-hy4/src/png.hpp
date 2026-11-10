// ============================================================================
//  png.hpp  --  Tiny, dependency-free PNG encoder.
//
//  Writes an 8-bit RGB or RGBA PNG using a valid zlib stream built entirely
//  from DEFLATE "stored" (type 00, uncompressed) blocks, so no zlib / libpng /
//  external library is needed. Includes CRC-32 and Adler-32.
//
//  pixels: tightly packed rows of `channels` bytes, row 0 = TOP of image.
//  If flipY is true the rows are reversed (handy for OpenGL glReadPixels,
//  which returns bottom-up data).
// ============================================================================
#ifndef BH_PNG_HPP
#define BH_PNG_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cstdio>

namespace bh {

inline uint32_t crc32(const uint8_t* data, size_t n){
    static uint32_t table[256];
    static bool built = false;
    if (!built){
        for (uint32_t i = 0; i < 256; ++i){
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline uint32_t adler32(const uint8_t* data, size_t n){
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i){
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

inline void putBE32(std::vector<uint8_t>& v, uint32_t x){
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}
inline void putLE16(std::vector<uint8_t>& v, uint16_t x){
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}

inline void pngChunk(std::vector<uint8_t>& out, const char type[4],
                     const std::vector<uint8_t>& data){
    putBE32(out, (uint32_t)data.size());
    size_t start = out.size();
    for (int i = 0; i < 4; ++i) out.push_back((uint8_t)type[i]);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = crc32(out.data() + start, 4 + data.size());
    putBE32(out, crc);
}

// channels: 3 (RGB) or 4 (RGBA). Returns true on success.
inline bool writePNG(const std::string& path, int w, int h, int channels,
                     const uint8_t* pixels, bool flipY){
    if (channels != 3 && channels != 4) return false;

    // ---- raw scanlines: each row prefixed by filter byte 0 (None) ----
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (1 + w * channels));
    for (int y = 0; y < h; ++y){
        int srcRow = flipY ? (h - 1 - y) : y;
        raw.push_back(0);                                   // filter: None
        const uint8_t* row = pixels + (size_t)srcRow * w * channels;
        raw.insert(raw.end(), row, row + (size_t)w * channels);
    }

    // ---- zlib stream: header + stored DEFLATE blocks + Adler-32 ----
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);                  // CM=8, CINFO=7, FCHECK
    size_t pos = 0;
    const size_t CHUNK = 65535;
    while (pos < raw.size()){
        size_t n = std::min(CHUNK, raw.size() - pos);
        bool last = (pos + n >= raw.size());
        z.push_back(last ? 0x01 : 0x00);                   // BFINAL + BTYPE=00
        putLE16(z, (uint16_t)n);
        putLE16(z, (uint16_t)(~n & 0xFFFF));               // NLEN = ~LEN
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    putBE32(z, adler32(raw.data(), raw.size()));

    // ---- assemble PNG ----
    std::vector<uint8_t> out;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    putBE32(ihdr, (uint32_t)w);
    putBE32(ihdr, (uint32_t)h);
    ihdr.push_back(8);                                      // bit depth
    ihdr.push_back(channels == 4 ? 6 : 2);                  // colour type RGBA/RGB
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

} // namespace bh
#endif // BH_PNG_HPP
