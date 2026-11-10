#include <algorithm>

#include <cmath>
#include <iostream>
#include <stdexcept>
struct V{double x,y; V operator+(V b)const{return{x+b.x,y+b.y};}V operator*(double a)const{return{x*a,y*a};}};
double dot(V a,V b){return a.x*b.x+a.y*b.y;} double norm(V a){return std::sqrt(dot(a,a));}double cross(V a,V b){return a.x*b.y-a.y*b.x;}
V acc(V p,double l2){return p*(-3*l2/std::pow(norm(p),5));}
void step(V& p,V& v,double l2,double h){V a=acc(p,l2),q=p+v*h+a*(0.5*h*h);v=v+(a+acc(q,l2))*(0.5*h);p=q;}
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
bool captured(double b){double r=30,f=1-2/r,s=b*std::sqrt(f)/r;V p={r,0},v={-std::sqrt(1-s*s)*std::sqrt(f),s};double l2=std::pow(cross(p,v),2);for(int i=0;i<2000;i++){r=norm(p);if(r<2.015)return true;if(r>48&&dot(p,v)>0)return false;step(p,v,l2,std::clamp(0.035*r,0.055,0.9));}throw std::runtime_error("capture ray did not terminate");}
int main(){try{
 const double critical=3*std::sqrt(3.0);require(captured(critical-0.05),"ray below critical impact parameter must be captured");require(!captured(critical+0.05),"ray above critical impact parameter must escape");
 V p={3,0},v={0,1/std::sqrt(3.0)};double l2=std::pow(cross(p,v),2);for(int i=0;i<10000;i++)step(p,v,l2,0.001);require(std::abs(norm(p)-3)<0.0001,"circular photon orbit is unstable numerically");
 p={-1000,50};v={1,0};l2=std::pow(cross(p,v),2);for(int i=0;i<23000&&p.x<1000;i++)step(p,v,l2,0.1);double angle=std::atan2(-v.y,v.x);require(angle>0.075&&angle<0.1,"weak-field bending is not near 4M/b");
 std::cout<<"PASS: photon sphere r=3M, capture threshold b=3 sqrt(3) M, weak-field bending = "<<angle<<" rad (4M/b = 0.08)\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
