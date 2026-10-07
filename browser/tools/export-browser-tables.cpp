#include <iostream>
#include <iomanip>
#define bh grokbh
#include "../../submissions/grok-4.7/src/physics.hpp"
#undef bh
#include "../../submissions/mimo-v2.6-flash/src/physics.hpp"
#include "../../submissions/claude-haiku-5.5-xhigh/src/disk_model.h"
#include "../../submissions/claude-sonnet-5.5-xhigh/src/physics.hpp"
template<class V> void arr(const V& v){std::cout<<"[";bool first=true;for(auto x:v){if(!first)std::cout<<",";first=false;std::cout<<x;}std::cout<<"]";}
int main(){std::cout<<std::setprecision(10);auto grok=grokbh::build_flux_table();auto mimo=bh::makeDiskParams(10,1e-7,false);auto hd=build_disk(.9,16,4096);
std::cout<<"{\"grok\":{\"rIn\":"<<grok.r_in<<",\"rOut\":"<<grok.r_out<<",\"flux\":";arr(grok.Fnorm);
std::cout<<"},\"mimo\":{\"F0\":"<<mimo.F0<<",\"FPeak\":"<<mimo.FPeak<<",\"tempScale\":"<<mimo.tempScale<<",\"bb\":";arr(bh::buildBlackbodyLUT(1024));
std::cout<<"},\"haiku\":{\"isco\":"<<hd.r_isco<<",\"flux\":";arr(hd.lut);
std::cout<<"},\"sonnet\":{\"gain\":"<<2.6/phys::blackbody_luminance(8500)<<",\"disk\":";arr(phys::nt_temperature_table(.9,20,1024));std::cout<<",\"bb\":[";bool first=true;for(auto v:phys::blackbody_table(1024,300,400000)){for(float x:{v.r,v.g,v.b,v.log2y}){if(!first)std::cout<<",";first=false;std::cout<<x;}}std::cout<<"]}}";}
