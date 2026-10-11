#include "pch.h"
#include "interpass_fixture.h"
#include <cmath>

int main(int argc, char** argv)
{
    try
    {
        if(argc!=3) throw std::runtime_error("Usage: interpass_experiments_quality shader-dir v21-baseline-dir");
        Runner r(true);
        auto standard=r.pipeline(argv[1],"DlssNr");
        auto baseline=r.pipeline(argv[2],"DlssNr");
        auto oldFast=r.pipeline(argv[2],"dlssnr_interpass_fast");
        auto fast=r.pipeline(argv[1],"dlssnr_interpass_fast");
        auto weights=r.pipeline(argv[1],"dlssnr_interpass_weights");
        auto cross=r.pipeline(argv[1],"dlssnr_interpass_cross5");
        auto low=r.pipeline(argv[1],"dlssnr_interpass_low36");
        unsigned checks=0;
        for(auto dims:{std::pair<UINT,UINT>{17,11},{65,37},{127,73}})
        for(int scale:{50,59,65})
        for(int stress=0;stress<4;++stress)
        for(bool half:{false,true})
        {
            const UINT ww=UINT(std::lround(dims.first*scale/100.0f)),wh=UINT(std::lround(dims.second*scale/100.0f));
            DlssNrConstants c{};c.Mode=33;c.Width=ww;c.Height=wh;
            c.ResidualScale=3.0f;c.ResidualBlend=0.015f;c.ResidualConfidenceSensitivity=1;
            if(stress==1){c.DirectResolveFlags=32;c.MvScaleX=1.37f;}
            if(stress==2){c.GuideWidth=1;c.MvScaleX=1.13f;c.MvScaleY=0.71f;}
            if(stress==3){c.GuideHeight=1;float lo=0.02f,hi=0.08f,floor=0.15f;
                std::memcpy(&c.ExposureSourceWidth,&lo,4);std::memcpy(&c.ExposureSourceHeight,&hi,4);
                std::memcpy(&c.ExposurePadding,&floor,4);}
            const auto reference=r.run(oldFast.Get(),dims.first,dims.second,ww,wh,c,stress,half);
            if(reference!=r.run(fast.Get(),dims.first,dims.second,ww,wh,c,stress,half))
                throw std::runtime_error("Fast v21 baseline changed");
            for(int mode:{33,35,36})
            {
                c.Mode=mode;auto* pso=mode==33?fast.Get():mode==35?weights.Get():cross.Get();
                const auto a=r.run(pso,dims.first,dims.second,ww,wh,c,stress,half);
                const auto b=r.run(standard.Get(),dims.first,dims.second,ww,wh,c,stress,half);
                if(mode==33 && b!=r.run(baseline.Get(),dims.first,dims.second,ww,wh,c,stress,half))
                    throw std::runtime_error("Fast v21 standard PSO result changed");
                for(size_t i=0;i<a.size();++i)
                {
                    if(half)
                    {
                        for(unsigned shift:{0u,16u})
                            if(std::abs(int((a[i]>>shift)&0xffff)-int((b[i]>>shift)&0xffff))>1)
                                throw std::runtime_error("Experiment PSO FP16 difference exceeds 1 ULP");
                    }
                    else
                    {
                        float av,bv;std::memcpy(&av,&a[i],4);std::memcpy(&bv,&b[i],4);
                        if(!std::isfinite(av)||!std::isfinite(bv)||std::abs(av-bv)>2e-6f)
                            throw std::runtime_error("Experiment PSO FP32 difference exceeds rounding tolerance");
                    }
                }
                for(size_t i=0;i<size_t(ww)*wh;++i)
                {
                    for(unsigned rgb=0;rgb<3;++rgb)
                    {
                        if(half){if(((a[i*2+rgb/2]>>(16*(rgb%2)))&0xffff)>0x3c00)
                            throw std::runtime_error("Nonfinite or out-of-range FP16 RGB");}
                        else{float v;std::memcpy(&v,&a[i*4+rgb],4);
                            if(!std::isfinite(v)||v<0||v>1) throw std::runtime_error("Invalid RGB");}
                    }
                    if(half ? (a[i*2+1]>>16)!=(reference[i*2+1]>>16) : a[i*4+3]!=reference[i*4+3])
                        throw std::runtime_error("Native alpha changed");
                }
                ++checks;
            }
            const UINT lw=(ww+7)/8,lh=(wh+7)/8;
            c={};c.Mode=18;c.Width=lw;c.Height=lh;
            const auto currentLow=r.run(standard.Get(),dims.first,dims.second,lw,lh,c,stress,half,8,ww,wh);
            if(currentLow!=r.run(baseline.Get(),dims.first,dims.second,lw,lh,c,stress,half,8,ww,wh))
                throw std::runtime_error("Normal/final Mode18 low filter changed");
            c.Mode=34;
            const auto a=r.run(low.Get(),dims.first,dims.second,lw,lh,c,stress,half,8,ww,wh);
            if(a!=r.run(standard.Get(),dims.first,dims.second,lw,lh,c,stress,half,8,ww,wh))
                throw std::runtime_error("Low36 PSO mismatch");
            const auto dc=r.run(low.Get(),dims.first,dims.second,lw,lh,c,stress,false,8,ww,wh,nullptr,1);
            for(size_t i=0;i<size_t(lw)*lh;++i)
            {
                for(unsigned rgb=0;rgb<3;++rgb)
                {
                    if(half){if((a[i*2+rgb/2]>>(16*(rgb%2))&0x7fff)>=0x7c00)
                        throw std::runtime_error("Nonfinite Low36 FP16 residual");}
                    else{float v;std::memcpy(&v,&a[i*4+rgb],4);if(!std::isfinite(v))
                        throw std::runtime_error("Nonfinite Low36 residual");}
                    float constant;std::memcpy(&constant,&dc[i*4+rgb],4);
                    if(std::abs(constant-(stress%2?-0.08f:0.08f))>1e-6f)
                        throw std::runtime_error("Low36 lost signed DC or clamped edge normalisation");
                }
            }
            ++checks;
        }
        std::cout<<"Experiments GPU contract PASS: "<<checks<<" checks; v21 Fast/Mode18 unchanged, isolated PSOs, signed DC, finite RGB/residual, alpha. Not perceptual validation.\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
