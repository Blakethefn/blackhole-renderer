// Original deterministic artistic star field; project MIT license.
// No astronomical catalog, external imagery, clock or platform RNG is used.
#include "tinyexr.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
uint32_t hash(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
double sample(uint32_t i,uint32_t channel) {return (hash(i*11+channel)+.5)/4294967296.0;}
}
int main(int argc,char** argv) {
    if(argc!=2) {std::fprintf(stderr,"Usage: generate-cinematic-stars OUTPUT.exr\n");return 1;}
    constexpr int width=2048,height=1024,count=1400;
    constexpr double pi=3.14159265358979323846;
    std::vector<float> rgb(width*height*3,0);
    for(uint32_t i=0;i<count;++i) {
        const double x=sample(i,0)*width;
        const double y=std::acos(1-2*sample(i,1))/pi*height;
        const double sigma=.3+.2*sample(i,2), intensity=.2+2*std::pow(sample(i,3),6);
        const double warmth=sample(i,4);
        const double color[]={.78+.22*warmth,.84+.13*warmth,1-.3*warmth};
        for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
            const int px=int(x)+dx,py=int(y)+dy;
            if(py<0||py>=height)continue;
            const double distance=(px+.5-x)*(px+.5-x)+(py+.5-y)*(py+.5-y);
            const double value=intensity*std::exp(-distance/(2*sigma*sigma));
            for(int c=0;c<3;++c) rgb[(py*width+(px+width)%width)*3+c]+=float(value*color[c]);
        }
    }
    const char* error=nullptr;
    const int result=SaveEXR(rgb.data(),width,height,3,1,argv[1],&error);
    if(result!=TINYEXR_SUCCESS) {std::fprintf(stderr,"Star asset write: %s\n",error?error:"unknown error");if(error)FreeEXRErrorMessage(error);return 1;}
    std::printf("Original linear-sRGB/D65 artistic sky: %dx%d, %d finite Gaussian stars, HALF RGB EXR\n",width,height,count);
}
