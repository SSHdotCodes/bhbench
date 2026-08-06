#include "text.hpp"

#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

#include <OpenGL/gl3.h>
#include <vector>
#include <cstdio>

namespace text {
namespace {

constexpr int  kFirst = 32;
constexpr int  kLast  = 126;
constexpr int  kCols  = 16;
constexpr int  kRows  = 8;      // 128 cells: 95 glyphs + a solid white cell

int   gCell = 0;
float gAdvance = 0, gAscent = 0, gSize = 0;
GLuint gTex = 0, gVao = 0, gVbo = 0, gProg = 0;
int   gW = 1, gH = 1;
int   gSolidCell = 127;

struct V { float x, y, u, v, r, g, b, a; };
std::vector<V> gVerts;

const char* kVert = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vCol;
void main(){
    vUV = aUV; vCol = aCol;
    vec2 p = vec2(aPos.x / uScreen.x, 1.0 - aPos.y / uScreen.y) * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
})";

const char* kFrag = R"(#version 410 core
in vec2 vUV; in vec4 vCol;
uniform sampler2D uAtlas;
layout(location=0) out vec4 fragColor;
void main(){
    float a = texture(uAtlas, vUV).r;
    fragColor = vec4(vCol.rgb, vCol.a * a);
})";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[1024]; glGetShaderInfoLog(s, 1024, nullptr, b); std::fprintf(stderr, "text shader: %s\n", b); }
    return s;
}

}  // namespace

bool init(float pixelSize) {
    gSize = pixelSize;
    gCell = (int)(pixelSize * 1.9f);

    CTFontRef font = CTFontCreateWithName(CFSTR("Menlo-Regular"), pixelSize, nullptr);
    if (!font) font = CTFontCreateWithName(CFSTR("Monaco"), pixelSize, nullptr);
    if (!font) return false;

    gAscent = (float)CTFontGetAscent(font);

    int W = kCols * gCell, H = kRows * gCell;
    std::vector<unsigned char> gray(size_t(W) * H, 0);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(gray.data(), W, H, 8, W, cs, kCGImageAlphaNone);
    CGColorSpaceRelease(cs);
    if (!ctx) { CFRelease(font); return false; }

    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, false);

    // Measure the advance of a representative glyph (the font is monospaced).
    {
        UniChar ch = 'M';
        CGGlyph gl = 0;
        CTFontGetGlyphsForCharacters(font, &ch, &gl, 1);
        CGSize adv;
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &gl, &adv, 1);
        gAdvance = (float)adv.width;
    }

    for (int c = kFirst; c <= kLast; ++c) {
        int idx = c - kFirst;
        int col = idx % kCols, row = idx / kCols;
        UniChar ch = (UniChar)c;
        CGGlyph gl = 0;
        if (!CTFontGetGlyphsForCharacters(font, &ch, &gl, 1)) continue;
        // CoreGraphics origin is bottom-left.
        CGPoint pos = CGPointMake(col * gCell + gCell * 0.12,
                                  H - (row + 1) * gCell + (gCell - gAscent) * 0.5 + gCell * 0.14);
        CTFontDrawGlyphs(font, &gl, &pos, 1, ctx);
    }

    // Solid white cell used to draw the HUD's translucent panels.
    {
        int col = gSolidCell % kCols, row = gSolidCell / kCols;
        CGContextFillRect(ctx, CGRectMake(col * gCell, H - (row + 1) * gCell, gCell, gCell));
    }

    CGContextRelease(ctx);
    CFRelease(font);

    glGenTextures(1, &gTex);
    glBindTexture(GL_TEXTURE_2D, gTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, gray.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    gProg = glCreateProgram();
    glAttachShader(gProg, vs); glAttachShader(gProg, fs);
    glLinkProgram(gProg);
    glDeleteShader(vs); glDeleteShader(fs);

    glGenVertexArrays(1, &gVao);
    glGenBuffers(1, &gVbo);
    glBindVertexArray(gVao);
    glBindBuffer(GL_ARRAY_BUFFER, gVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void shutdown() {
    if (gTex) glDeleteTextures(1, &gTex);
    if (gVbo) glDeleteBuffers(1, &gVbo);
    if (gVao) glDeleteVertexArrays(1, &gVao);
    if (gProg) glDeleteProgram(gProg);
    gTex = gVbo = gVao = gProg = 0;
}

void begin(int w, int h) { gW = w; gH = h; gVerts.clear(); }

float lineHeight(float scale) { return gSize * 1.45f * scale; }
float width(const std::string& s, float scale) { return gAdvance * scale * s.size(); }

static void quad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1,
                 float r, float g, float b, float a) {
    gVerts.push_back({x0, y0, u0, v0, r, g, b, a});
    gVerts.push_back({x1, y0, u1, v0, r, g, b, a});
    gVerts.push_back({x1, y1, u1, v1, r, g, b, a});
    gVerts.push_back({x0, y0, u0, v0, r, g, b, a});
    gVerts.push_back({x1, y1, u1, v1, r, g, b, a});
    gVerts.push_back({x0, y1, u0, v1, r, g, b, a});
}

void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    int col = gSolidCell % kCols, row = gSolidCell / kCols;
    float cu = 1.0f / kCols, cv = 1.0f / kRows;
    float u = (col + 0.5f) * cu, v = (row + 0.5f) * cv;
    quad(x, y, x + w, y + h, u, v, u, v, r, g, b, a);
}

void draw(float x, float y, float scale, float r, float g, float b, float a, const std::string& s) {
    float cw = gCell * scale, chh = gCell * scale;
    float cu = 1.0f / kCols, cv = 1.0f / kRows;
    float pen = x;
    for (unsigned char c : s) {
        if (c == ' ') { pen += gAdvance * scale; continue; }
        if (c < kFirst || c > kLast) { pen += gAdvance * scale; continue; }
        int idx = c - kFirst;
        int col = idx % kCols, row = idx / kCols;
        float u0 = col * cu, v0 = row * cv;
        float ox = -gCell * 0.12f * scale;
        float oy = -gCell * 0.30f * scale;
        quad(pen + ox, y + oy, pen + ox + cw, y + oy + chh, u0, v0, u0 + cu, v0 + cv, r, g, b, a);
        pen += gAdvance * scale;
    }
}

void end() {
    if (gVerts.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(gProg);
    glUniform2f(glGetUniformLocation(gProg, "uScreen"), (float)gW, (float)gH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gTex);
    glUniform1i(glGetUniformLocation(gProg, "uAtlas"), 0);

    glBindVertexArray(gVao);
    glBindBuffer(GL_ARRAY_BUFFER, gVbo);
    glBufferData(GL_ARRAY_BUFFER, gVerts.size() * sizeof(V), gVerts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)gVerts.size());
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace text
