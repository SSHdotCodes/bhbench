#pragma once
#include "graphics.hpp"
#include <array>
#include <vector>
struct Color {float r,g,b,a=1;};
inline constexpr Color ink{.86f,.89f,.89f,1},muted{.43f,.50f,.54f,1},gold{.91f,.68f,.36f,1},cyan{.39f,.74f,.80f,1};
class UI {
public:
    void init();void begin(float w,float h,float mouseX,float mouseY,bool pressed,bool down);
    void input(float mouseX,float mouseY,bool pressed,bool down){mx=mouseX;my=mouseY;click=pressed;held=down;}
    void rect(float x,float y,float w,float h,Color color);
    void line(float x,float y,float w,Color color);
    void text(float x,float y,const std::string&s,float size,Color color=ink);
    float textWidth(const std::string&s,float size)const;
    bool button(float x,float y,float w,float h,const std::string&label,bool selected=false);
    bool toggle(float x,float y,float w,const std::string&label,bool&value);
    void slider(int id,float x,float y,float w,float&value,float low,float high);
    void draw(int framebufferWidth,int framebufferHeight);
    bool hover(float x,float y,float w,float h)const;
    ~UI();
private:
    struct Glyph {float u,v,w,h,bx,by,advance;};
    struct Vertex {float x,y,u,v;Color color;};
    std::array<Glyph,128> glyphs{};std::vector<Vertex>vertices;
    GLuint prog=0,vao=0,vbo=0,atlas=0;
    float width=0,height=0,mx=0,my=0;bool click=false,held=false;int active=-1;
    void quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color c);
};
