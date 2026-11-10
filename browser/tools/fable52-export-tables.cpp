#include "submission/src/physics.hpp"
#include "submission/src/blackbody.hpp"
#include <iostream>
#include <iomanip>
int main(){
  auto bb=blackbody::build();kerr::DiskFlux flux;flux.build(.9,60);
  std::cout<<std::setprecision(10)<<"{\"logTmin\":"<<bb.logTmin<<",\"logTmax\":"<<bb.logTmax<<",\"logLumRef\":"<<bb.logLuminance(5000)<<",\"bb\":[";
  for(size_t i=0;i<bb.rgba.size();i++)std::cout<<(i?",":"")<<bb.rgba[i];
  std::cout<<"],\"disk\":[";
  for(int i=0;i<1024;i++){double u=double(i)/1023,r=flux.rIn+(60-flux.rIn)*u*u;std::cout<<(i?",":"")<<float(pow(std::max(flux.flux(r),0.)/flux.Fmax,.25));}
  std::cout<<"],\"embedding\":[";auto e=kerr::embedding(.9,46,200);
  for(size_t i=0;i<e.size();i++){auto v=e[i];std::cout<<(i?",":"")<<"["<<v.r<<","<<v.Rc<<","<<v.z-e.back().z<<","<<v.tau<<","<<v.ell<<","<<v.lapse<<"]";}
  std::cout<<"],\"horizon\":"<<kerr::horizon(.9)<<",\"isco\":"<<kerr::isco(.9)<<",\"photonCo\":"<<kerr::photonOrbit(.9,true)<<",\"photonCounter\":"<<kerr::photonOrbit(.9,false)<<"}\n";
}
