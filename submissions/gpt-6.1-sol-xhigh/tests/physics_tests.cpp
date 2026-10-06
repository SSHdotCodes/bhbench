#include "physics.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
int failures=0;
void check(bool ok,const std::string& message) {
    std::cout<<(ok?"PASS  ":"FAIL  ")<<message<<'\n'; failures+=!ok;
}
int main() {
    using namespace bh;
    std::cout<<std::setprecision(12);
    Orbit circular{1./3,0,0};
    for(int i=0;i<2000;++i) circular=rk4(circular,.01,criticalImpact);
    check(std::abs(circular.u-1./3)<1e-10,"Unstable photon orbit remains at r = 3M for exact initial data");
    for(double b:{4.8,5.19,5.20,5.5,8.,20.}) {
        double R=100, f=1-2/R, nr=-std::sqrt(1-b*b*f/(R*R));
        auto end=trace(R,nr,.008);
        check(end.status==(b<criticalImpact?0:1),"Capture / escape across b_crit, b = "+std::to_string(b));
    }
    double maximum=0;
    Orbit s=initial(100,-std::sqrt(1-64*.98/10000));
    const double inv=invariant(s);
    for(int i=0;i<300;++i) {
        maximum=std::max(maximum,std::abs(invariant(s)-inv));
        s=rk4(s,.012,8); if(s.u<=0) break;
    }
    check(maximum<1e-9,"Null first integral conserved to < 1e-9 (double precision)");
    auto scattering=[](double h) {
        const double b=8,R=100;
        return trace(R,-std::sqrt(1-b*b*(1-2/R)/(R*R)),h).phi;
    };
    double reference=scattering(.0005), coarse=std::abs(scattering(.04)-reference), fine=std::abs(scattering(.02)-reference);
    // Cubic dense output preserves high-order convergence at the infinity crossing.
    check(fine<coarse*.4,"Refining the integrator improves asymptotic ray direction");
    const double b=1000,R=1e7;
    double nr=-std::sqrt(1-b*b*(1-2/R)/(R*R));
    double alpha=trace(R,nr,.001).phi-pi+std::asin(b/R);
    check(std::abs(alpha/(4/b)-1)<.005,"Weak-field bending approaches 4M/b");
    check(diskFlux(6)==0 && diskFlux(7)>0,"Zero torque at ISCO, positive flux outside");
    // Independently integrate (E-Omega*L)*dL/dr to validate the closed form flux.
    double integral=0; const int N=10000; const double dr=14./N;
    for(int i=0;i<N;++i) {
        double r=6+(i+.5)*dr;
        integral+=(r-6)/(2*std::sqrt(r)*(r-3))*dr;
    }
    double numerical=1.5*integral/(std::pow(20.,2.5)*17);
    check(std::abs(numerical/diskFlux(20)-1)<1e-8,"Novikov-Thorne flux matches independent conservation-law quadrature");
    check(std::abs(frequencyShift(10,100,0)-std::sqrt(.7/.98))<1e-14,"Face-on shift includes gravitational and transverse Doppler terms");
    check(frequencyShift(10,100,5)>frequencyShift(10,100,-5),"Approaching emitter is brighter via g^4");
    double r=8,h=1e-5, dz=(embeddingHeight(r+h)-embeddingHeight(r-h))/(2*h);
    check(std::abs(1+dz*dz-1/(1-2/r))<1e-9,"Flamm embedding reproduces the exact spatial radial metric");
    std::cout<<"Maximum invariant drift: "<<maximum<<"\nWeak-field bending: "<<alpha<<" rad\n";
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
