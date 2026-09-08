#include "ui.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdexcept>
#include <cstddef>
#include <algorithm>

void UI::init(){
    FT_Library library;FT_Face face;if(FT_Init_FreeType(&library))throw std::runtime_error("FreeType init failed");
    const char*fonts[]={"/System/Library/Fonts/SFNS.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf"};bool loaded=false;
    for(auto path:fonts)if(!FT_New_Face(library,path,0,&face)){loaded=true;break;}
    if(!loaded)throw std::runtime_error("No UI font available");FT_Set_Pixel_Sizes(face,0,72);
    std::vector<unsigned char>pixels(2048*1024,0);
    for(int c=32;c<127;c++){
        if(FT_Load_Char(face,c,FT_LOAD_RENDER))continue;auto*g=face->glyph;int x=(c-32)%16*120,y=(c-32)/16*130;
        for(unsigned int row=0;row<g->bitmap.rows;row++)for(unsigned int col=0;col<g->bitmap.width;col++)pixels[(y+row)*2048+x+col]=g->bitmap.buffer[row*g->bitmap.pitch+col];
        glyphs[c]={x/2048.f,y/1024.f,float(g->bitmap.width),float(g->bitmap.rows),float(g->bitmap_left),float(g->bitmap_top),float(g->advance.x)/64.f};
    }
    pixels.back()=255;FT_Done_Face(face);FT_Done_FreeType(library);
    glGenTextures(1,&atlas);glBindTexture(GL_TEXTURE_2D,atlas);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_R8,2048,1024,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    prog=program(R"(#version 410 core
layout(location=0)in vec2 position;layout(location=1)in vec2 texcoord;layout(location=2)in vec4 color;
uniform vec2 viewportSize;out vec2 uv;out vec4 tint;
void main(){uv=texcoord;tint=color;gl_Position=vec4(position.x/viewportSize.x*2.-1.,1.-position.y/viewportSize.y*2.,0,1);})",R"(#version 410 core
in vec2 uv;in vec4 tint;uniform sampler2D atlas;out vec4 outColor;
void main(){outColor=vec4(tint.rgb,tint.a*texture(atlas,uv).r);})");
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,u));glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,color));glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
}
void UI::begin(float w,float h,float mouseX,float mouseY,bool pressed,bool down){width=w;height=h;mx=mouseX;my=mouseY;click=pressed;held=down;if(!held)active=-1;vertices.clear();}
void UI::quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color c){vertices.insert(vertices.end(),{{x,y,u,v,c},{x+w,y,u+uw,v,c},{x+w,y+h,u+uw,v+vh,c},{x,y,u,v,c},{x+w,y+h,u+uw,v+vh,c},{x,y+h,u,v+vh,c}});}
void UI::rect(float x,float y,float w,float h,Color c){quad(x,y,w,h,2047.5f/2048,1023.5f/1024,0,0,c);}
void UI::line(float x,float y,float w,Color c){rect(x,y,w,1,c);}
void UI::text(float x,float y,const std::string&s,float size,Color c){float scale=size/72.f;for(unsigned char ch:s){if(ch>=128)continue;auto&g=glyphs[ch];quad(x+g.bx*scale,y+size-g.by*scale,g.w*scale,g.h*scale,g.u,g.v,g.w/2048,g.h/1024,c);x+=g.advance*scale;}}
float UI::textWidth(const std::string&s,float size)const{float w=0;for(unsigned char c:s)if(c<128)w+=glyphs[c].advance*size/72;return w;}
bool UI::hover(float x,float y,float w,float h)const{return mx>=x&&mx<=x+w&&my>=y&&my<=y+h;}
bool UI::button(float x,float y,float w,float h,const std::string&label,bool selected){bool hot=hover(x,y,w,h);rect(x,y,w,h,selected?Color{.15f,.13f,.10f,1}:(hot?Color{.10f,.13f,.15f,1}:Color{.043f,.060f,.072f,1}));if(selected)rect(x,y+h-2,w,2,gold);text(x+(w-textWidth(label,12))*.5f,y+(h-14)*.5f,label,12,selected?gold:ink);return hot&&click;}
bool UI::toggle(float x,float y,float w,const std::string&label,bool&value){bool hot=hover(x,y,w,32);if(hot)rect(x-8,y-3,w+16,34,{.075f,.094f,.103f,1});if(hot&&click)value=!value;text(x,y+5,label,13,value?ink:muted);rect(x+w-30,y+9,30,13,value?Color{.43f,.32f,.17f,1}:Color{.12f,.16f,.18f,1});rect(x+w-(value?12:27),y+11,9,9,value?gold:muted);return hot&&click;}
void UI::slider(int id,float x,float y,float w,float&value,float low,float high){bool started=click&&hover(x-6,y-10,w+12,28);if(started)active=id;if(started||(held&&active==id))value=low+(high-low)*std::clamp((mx-x)/w,0.f,1.f);float t=(value-low)/(high-low);rect(x,y,w,2,{.14f,.19f,.21f,1});rect(x,y,w*t,2,gold);rect(x+w*t-3,y-4,6,10,gold);}
void UI::draw(int fw,int fh){glViewport(0,0,fw,fh);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(prog);glUniform2f(glGetUniformLocation(prog,"viewportSize"),width,height);uniform(prog,"atlas",0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,int(vertices.size()));glDisable(GL_BLEND);}
UI::~UI(){if(prog)glDeleteProgram(prog);if(atlas)glDeleteTextures(1,&atlas);if(vbo)glDeleteBuffers(1,&vbo);if(vao)glDeleteVertexArrays(1,&vao);}
