#include "text.h"

#include <cstring>
#include <vector>

#include "glutil.h"
#include "shaders_embedded.h"

namespace text {

namespace {

constexpr int kCellW = 6;   // 5 px glyph + 1 px padding
constexpr int kCellH = 8;   // 7 px glyph + 1 px padding
constexpr int kCols = 16;
constexpr int kRows = 6;
constexpr int kAtlasW = kCols * kCellW;
constexpr int kAtlasH = kRows * kCellH;

// 5x7 glyphs, row-major, '#' = ink.
struct GlyphDef {
    char c;
    const char* bits;  // 35 characters
};

const GlyphDef kGlyphs[] = {
    {' ', "....." "....." "....." "....." "....." "....." "....."},
    {'A', ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'B', "####." "#...#" "#...#" "####." "#...#" "#...#" "####."},
    {'C', ".###." "#...#" "#...." "#...." "#...." "#...#" ".###."},
    {'D', "####." "#...#" "#...#" "#...#" "#...#" "#...#" "####."},
    {'E', "#####" "#...." "#...." "####." "#...." "#...." "#####"},
    {'F', "#####" "#...." "#...." "####." "#...." "#...." "#...."},
    {'G', ".###." "#...#" "#...." "#.###" "#...#" "#...#" ".###."},
    {'H', "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'I', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "#####"},
    {'J', "..###" "...#." "...#." "...#." "...#." "#..#." ".##.."},
    {'K', "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#"},
    {'L', "#...." "#...." "#...." "#...." "#...." "#...." "#####"},
    {'M', "#...#" "##.##" "#.#.#" "#...#" "#...#" "#...#" "#...#"},
    {'N', "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#" "#...#"},
    {'O', ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'P', "####." "#...#" "#...#" "####." "#...." "#...." "#...."},
    {'Q', ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#"},
    {'R', "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#"},
    {'S', ".####" "#...." "#...." ".###." "....#" "....#" "####."},
    {'T', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'U', "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'V', "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#.."},
    {'W', "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "##.##" "#...#"},
    {'X', "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#"},
    {'Y', "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#.."},
    {'Z', "#####" "....#" "...#." "..#.." ".#..." "#...." "#####"},
    {'0', ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###."},
    {'1', "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'2', ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####"},
    {'3', "#####" "...#." "..#.." "...#." "....#" "#...#" ".###."},
    {'4', "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#."},
    {'5', "#####" "#...." "####." "....#" "....#" "#...#" ".###."},
    {'6', "..##." ".#..." "#...." "####." "#...#" "#...#" ".###."},
    {'7', "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#..."},
    {'8', ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###."},
    {'9', ".###." "#...#" "#...#" ".####" "....#" "...#." ".##.."},
    {'.', "....." "....." "....." "....." "....." ".##.." ".##.."},
    {',', "....." "....." "....." "....." ".##.." ".##.." ".#..."},
    {':', "....." ".##.." ".##.." "....." ".##.." ".##.." "....."},
    {';', "....." ".##.." ".##.." "....." ".##.." ".##.." ".#..."},
    {'-', "....." "....." "....." "#####" "....." "....." "....."},
    {'+', "....." "..#.." "..#.." "#####" "..#.." "..#.." "....."},
    {'/', "....#" "....#" "...#." "..#.." ".#..." "#...." "#...."},
    {'(', "...#." "..#.." ".#..." ".#..." ".#..." "..#.." "...#."},
    {')', ".#..." "..#.." "...#." "...#." "...#." "..#.." ".#..."},
    {'[', ".###." ".#..." ".#..." ".#..." ".#..." ".#..." ".###."},
    {']', ".###." "...#." "...#." "...#." "...#." "...#." ".###."},
    {'<', "...#." "..#.." ".#..." "#...." ".#..." "..#.." "...#."},
    {'>', ".#..." "..#.." "...#." "....#" "...#." "..#.." ".#..."},
    {'=', "....." "....." "#####" "....." "#####" "....." "....."},
    {'_', "....." "....." "....." "....." "....." "....." "#####"},
    {'%', "##..#" "##.#." "..#.." ".#..." "#..##" "#..##" "....."},
    {'*', "....." "#.#.#" ".###." "#####" ".###." "#.#.#" "....."},
    {'!', "..#.." "..#.." "..#.." "..#.." "..#.." "....." "..#.."},
    {'?', ".###." "#...#" "....#" "...#." "..#.." "....." "..#.."},
    {'\'', "..#.." "..#.." "....." "....." "....." "....." "....."},
    {'"', ".#.#." ".#.#." "....." "....." "....." "....." "....."},
    {'|', "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'#', ".#.#." ".#.#." "#####" ".#.#." "#####" ".#.#." ".#.#."},
    {'@', ".###." "#...#" "#.###" "#.#.#" "#.###" "#...." ".###."},
    {'&', ".##.." "#..#." ".##.." "#.#.#" "#..#." "#..#." ".##.#"},
    {'~', "....." "....." ".##.#" "#.##." "....." "....." "....."},
};

// Non-ASCII symbols mapped into spare atlas cells.
constexpr int kCellDegree = 95;
constexpr int kCellTimes = 96;
constexpr int kCellApprox = 97;

const GlyphDef kExtra[] = {
    {'\x01', ".###." "#...#" "#...#" ".###." "....." "....." "....."},  // degree
    {'\x02', "....." "#...#" ".#.#." "..#.." ".#.#." "#...#" "....."},  // times
    {'\x03', "....." ".##.#" "#.##." "....." ".##.#" "#.##." "....."},  // approx
};

int cellFor(char c) {
    if (c == '\x01') return kCellDegree;
    if (c == '\x02') return kCellTimes;
    if (c == '\x03') return kCellApprox;
    int idx = static_cast<unsigned char>(c) - 32;
    if (idx < 0 || idx >= kCols * kRows) return 0;
    return idx;
}

GLuint buildAtlas() {
    std::vector<unsigned char> px(kAtlasW * kAtlasH, 0);

    auto stamp = [&](int cell, const char* bits) {
        int cx = (cell % kCols) * kCellW;
        int cy = (cell / kCols) * kCellH;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (bits[row * 5 + col] == '#')
                    px[(cy + row) * kAtlasW + (cx + col)] = 255;
    };

    for (const auto& g : kGlyphs) stamp(cellFor(g.c), g.bits);
    for (const auto& g : kExtra) stamp(cellFor(g.c), g.bits);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED,
                 GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

}  // namespace

void Renderer::init() {
    program_ = glu::makeProgram(SHADER_TEXT_VERT, SHADER_TEXT_FRAG, "text");
    atlas_ = buildAtlas();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vert),
                          (void*)(4 * sizeof(float)));
    glBindVertexArray(0);
}

void Renderer::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (atlas_) glDeleteTextures(1, &atlas_);
    if (program_) glDeleteProgram(program_);
}

void Renderer::begin(int screenW, int screenH) {
    screenW_ = screenW;
    screenH_ = screenH;
    verts_.clear();
}

void Renderer::draw(float x, float y, float scale, const std::string& s,
                    float r, float g, float b, float a) {
    float cx = x;
    for (char ch : s) {
        if (ch == '\n') {
            cx = x;
            y += 8.0f * scale;
            continue;
        }
        int cell = cellFor(ch);
        int gx = (cell % kCols) * kCellW;
        int gy = (cell / kCols) * kCellH;
        float u0 = float(gx) / kAtlasW;
        float v0 = float(gy) / kAtlasH;
        float u1 = float(gx + 5) / kAtlasW;
        float v1 = float(gy + 7) / kAtlasH;

        float x0 = cx, y0 = y;
        float x1 = cx + 5.0f * scale, y1 = y + 7.0f * scale;

        verts_.push_back({x0, y0, u0, v0, r, g, b, a});
        verts_.push_back({x1, y0, u1, v0, r, g, b, a});
        verts_.push_back({x1, y1, u1, v1, r, g, b, a});
        verts_.push_back({x0, y0, u0, v0, r, g, b, a});
        verts_.push_back({x1, y1, u1, v1, r, g, b, a});
        verts_.push_back({x0, y1, u0, v1, r, g, b, a});

        cx += 6.0f * scale;
    }
}

void Renderer::end() {
    if (verts_.empty()) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(program_);
    glUniform2f(glGetUniformLocation(program_, "uScreen"),
                float(screenW_), float(screenH_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glUniform1i(glGetUniformLocation(program_, "uAtlas"), 0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts_.size() * sizeof(Vert)),
                 verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    verts_.clear();
}

}  // namespace text
