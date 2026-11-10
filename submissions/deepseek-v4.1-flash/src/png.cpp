#include "png.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace png {

namespace {

uint32_t crcTable[256];
bool crcReady = false;

void initCrc() {
    if (crcReady) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    crcReady = true;
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    initCrc();
    for (size_t i = 0; i < len; ++i)
        crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void writeChunk(std::vector<uint8_t>& out, const char type[4],
                const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t c = crc32(out.data() + start, out.size() - start);
    put32(out, c ^ 0xFFFFFFFFu);
}

// zlib stream using uncompressed deflate blocks.
std::vector<uint8_t> zlibStore(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.push_back(0x78);  // CMF: deflate, 32K window
    out.push_back(0x01);  // FLG: no dict, fastest; (0x7801 % 31 == 0)

    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = (pos + n >= raw.size());
        out.push_back(last ? 1 : 0);
        out.push_back(uint8_t(n & 0xFF));
        out.push_back(uint8_t((n >> 8) & 0xFF));
        uint16_t nlen = static_cast<uint16_t>(~n);
        out.push_back(uint8_t(nlen & 0xFF));
        out.push_back(uint8_t((nlen >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put32(out, adler32(raw.data(), raw.size()));
    return out;
}

}  // namespace

bool write(const char* path, int width, int height, int channels,
           const uint8_t* pixels) {
    if (channels != 3 && channels != 4) return false;

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + width * channels));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        const uint8_t* row = pixels + static_cast<size_t>(y) * width * channels;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * channels);
    }

    std::vector<uint8_t> out;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);                                  // bit depth
    ihdr.push_back(channels == 4 ? 6 : 2);              // colour type
    ihdr.push_back(0);                                  // compression
    ihdr.push_back(0);                                  // filter
    ihdr.push_back(0);                                  // interlace
    writeChunk(out, "IHDR", ihdr);

    writeChunk(out, "IDAT", zlibStore(raw));
    writeChunk(out, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return written == out.size();
}

}  // namespace png
