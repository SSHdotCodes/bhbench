// Shader loading, PNG capture (zlib), small utilities.
#pragma once
#include <OpenGL/gl3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <zlib.h>

inline std::string loadText(const std::string& path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(1); }
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

inline GLuint compileShader(GLenum type, const std::string& src, const std::string& name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "shader compile failed [%s]:\n%s\n", name.c_str(), log);
        exit(1);
    }
    return s;
}

inline GLuint loadProgram(const std::string& dir, const std::string& vs, const std::string& fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, loadText(dir + "/" + vs), vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, loadText(dir + "/" + fs), fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "link failed [%s/%s]:\n%s\n", vs.c_str(), fs.c_str(), log);
        exit(1);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ---- PNG writer (RGB8, filter 0, zlib via compress2) ----
inline void putChunk(FILE* f, const char* type, const unsigned char* data, size_t n) {
    unsigned char be[4] = {(unsigned char)(n>>24), (unsigned char)(n>>16), (unsigned char)(n>>8), (unsigned char)n};
    fwrite(be, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef*)type, 4);
    if (n) crc = crc32(crc, data, (uInt)n);
    unsigned char cb[4] = {(unsigned char)(crc>>24), (unsigned char)(crc>>16), (unsigned char)(crc>>8), (unsigned char)crc};
    fwrite(cb, 1, 4, f);
}

inline bool savePNG(const std::string& path, int w, int h, const unsigned char* rgb) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13] = {
        (unsigned char)(w>>24),(unsigned char)(w>>16),(unsigned char)(w>>8),(unsigned char)w,
        (unsigned char)(h>>24),(unsigned char)(h>>16),(unsigned char)(h>>8),(unsigned char)h,
        8, 2, 0, 0, 0};
    putChunk(f, "IHDR", ihdr, 13);
    std::vector<unsigned char> raw((size_t)(w*3+1)*h);
    for (int y = 0; y < h; ++y) {
        raw[(size_t)y*(w*3+1)] = 0;
        memcpy(&raw[(size_t)y*(w*3+1)+1], rgb + (size_t)(h-1-y)*w*3, (size_t)w*3); // flip vertically
    }
    uLongf compLen = compressBound((uLong)raw.size());
    std::vector<unsigned char> comp(compLen);
    if (compress2(comp.data(), &compLen, raw.data(), (uLong)raw.size(), 6) != Z_OK) { fclose(f); return false; }
    putChunk(f, "IDAT", comp.data(), compLen);
    putChunk(f, "IEND", nullptr, 0);
    fclose(f);
    return true;
}
