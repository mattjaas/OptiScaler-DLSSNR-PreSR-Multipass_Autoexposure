#include "pch.h"
#include "interpass_fixture.h"
#include <cmath>

// Check actual compiled Mode33 and isolated Fast PSO, including odd dimensions,
// frame edges, FP16 output, non-finite/HDR inputs, shaping, and native alpha.
// The approximate result must NOT be required to equal the exact guided filter.
int main(int argc, char** argv)
{
    try
    {
        if(argc<2) throw std::runtime_error("Usage: interpass_fast_quality shader-dir [--hardware]");
        Runner runner(argc>2 && std::string(argv[2])=="--hardware");
        auto standard=runner.pipeline(argv[1],"DlssNr");
        auto fast=runner.pipeline(argv[1],"dlssnr_interpass_fast");
        unsigned fixtures=0;
        for(auto dims:{std::pair<UINT,UINT>{17,11},{65,37},{127,73}})
        for(int scale:{50,59,65})
        for(int stress=0;stress<4;++stress)
        for(bool half:{false,true})
        {
            UINT ww=UINT(std::lround(dims.first*scale/100.0f)),wh=UINT(std::lround(dims.second*scale/100.0f));
            DlssNrConstants c{};c.Mode=33;c.Width=ww;c.Height=wh;
            c.ResidualHistoryValid=1;c.ResidualScale=1.2f;c.ResidualBlend=0.015f;
            c.ResidualConfidenceSensitivity=0.75f;
            if(stress==1){c.DirectResolveFlags=32;c.MvScaleX=1.37f;}
            if(stress==2){c.GuideWidth=1;c.MvScaleX=1.13f;c.MvScaleY=0.71f;}
            if(stress==3){c.GuideHeight=1;float lo=0.02f,hi=0.08f,floor=0.15f;
                std::memcpy(&c.ExposureSourceWidth,&lo,4);std::memcpy(&c.ExposureSourceHeight,&hi,4);
                std::memcpy(&c.ExposurePadding,&floor,4);}
            const auto a=runner.run(fast.Get(),dims.first,dims.second,ww,wh,c,stress,half);
            const auto b=runner.run(standard.Get(),dims.first,dims.second,ww,wh,c,stress,half);
            if(a!=b) throw std::runtime_error("Fast isolated/standard PSO mismatch");
            c.Mode=28;
            const auto exact=runner.run(standard.Get(),dims.first,dims.second,ww,wh,c,stress,half);
            for(size_t pixel=0;pixel<size_t(ww)*wh;++pixel)
            {
                for(unsigned rgb=0;rgb<3;++rgb)
                {
                    if(half){const auto bits=(a[pixel*2+rgb/2]>>(16*(rgb%2)))&0xffff;
                        if(bits>0x3c00) throw std::runtime_error("Fast FP16 RGB outside finite proxy range");}
                    else{float v;std::memcpy(&v,&a[pixel*4+rgb],4);
                        if(!std::isfinite(v)||v<0||v>1) throw std::runtime_error("Fast RGB outside finite proxy range");}
                }
                if(half){if((a[pixel*2+1]>>16)!=(exact[pixel*2+1]>>16))
                    throw std::runtime_error("Fast changed native alpha");}
                else if(a[pixel*4+3]!=exact[pixel*4+3]) throw std::runtime_error("Fast changed native alpha");
            }
            ++fixtures;
        }
        std::cout<<"Fast GPU contract PASS: "<<fixtures<<" fixtures; PSO equivalence, finite RGB and native alpha. Not a perceptual quality verdict.\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
