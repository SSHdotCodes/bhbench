#include "physics.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>

int main(){int failures=0;auto check=[&](bool ok,const std::string&name,double error=0.){std::cout<<(ok?"PASS ":"FAIL ")<<name<<" (error="<<std::setprecision(9)<<error<<")\n";if(!ok)failures++;};
    using namespace physics;
    auto below=trace(criticalImpact()*(1.-1.e-5)),above=trace(criticalImpact()*(1.+1.e-5));check(below.captured&&!below.escaped&&above.escaped&&!above.captured,"Capture separatrix b = 3 sqrt(3) M");
    State circular{1./3.,0.,0.};State c=circular;for(int i=0;i<6283;i++)c=rk4(c,.001,criticalImpact());double drift=std::abs(c.u-circular.u);check(drift<1.e-9,"Unstable circular photon orbit at 3M",drift);
    for(double b:{5.2,6.,10.,30.}){auto ray=trace(b,.01);check(ray.escaped&&ray.maxRelativeError<1.e-7,"Conserved null invariant, b="+std::to_string(b),ray.maxRelativeError);}
    // The weak-field analytic series is independent of the numerical ODE solver.
    double b=200.,predicted=4./b+15.*pi/(4.*b*b)+128./(3.*b*b*b)+3465.*pi/(64.*std::pow(b,4));
    auto weak=trace(b,.002,200000.);double weakError=std::abs((weak.angle-pi)-predicted);check(weakError<1.e-8,"Weak deflection through fourth order in M/b",weakError);
    auto flat=trace(10.,.003,2000.,false);check(std::abs(flat.angle-pi)<1.e-6,"Flat-space control follows a straight line",std::abs(flat.angle-pi));
    auto coarse=trace(5.3,.08),medium=trace(5.3,.04),fine=trace(5.3,.02),ref=trace(5.3,.001);
    double e1=std::abs(coarse.angle-ref.angle),e2=std::abs(medium.angle-ref.angle),e3=std::abs(fine.angle-ref.angle);check(e1>e2*8&&e2>e3*8,"Fourth-order geodesic convergence",e3);
    double r=10.,delta=1.e-5,dz=(embedding(r+delta)-embedding(r-delta))/(2.*delta);check(std::abs((1.+dz*dz)-1./(1.-2./r))<1.e-9,"Embedding reproduces Schwarzschild spatial metric",std::abs(1.+dz*dz-1./(1.-2./r)));
    double g=redshift(6.,42.,0.),expected=std::sqrt(.5/(1.-2./42.));check(std::abs(g-expected)<1.e-14,"Transverse + gravitational redshift at ISCO",std::abs(g-expected));
    check(redshift(10.,42.,4.)>redshift(10.,42.,-4.),"Approaching material is blueshifted relative to receding material");
    check(diskFlux(6.)==0&&diskFlux(5.)==0&&diskFlux(10.)>0,"Zero-torque disk boundary at 6M");
    double peak=0,peakR=0;for(double r=6.;r<40;r+=.001)if(diskFlux(r)>peak){peak=diskFlux(r);peakR=r;}check(peakR>9.4&&peakR<9.7,"Relativistic flux peaks outside Newtonian peak",peakR);std::cout<<"Flux normalization = "<<std::setprecision(12)<<peak<<" at r="<<peakR<<"\n";
    check(std::abs((2.*pi/omega(6.))-92.3435877716)<1.e-8,"Coordinate orbital period at ISCO",std::abs(2.*pi/omega(6.)-92.3435877716));
    std::cout<<(failures?"FAILED":"ALL PHYSICS CHECKS PASSED")<<"\n";return failures?1:0;
}
