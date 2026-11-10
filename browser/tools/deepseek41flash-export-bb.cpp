#include "blackbody.h"
#include <cstdio>
int main(){auto v=bb::buildLUT(1024,bb::kLutMinT,bb::kLutMaxT);std::puts("[");for(size_t i=0;i<v.size();i++)std::printf("%.9g%s",v[i],i+1<v.size()?",":"");std::puts("]");}
