#include "pch.h"
#include "interpass_timing_fixture.h"

// Resident inter-pass GPU timing with unequal high/low shaping; NOT whole NR or game timing.
int main(int argc, char** argv)
{
    try
    {
        if(argc<3 || argc>5) throw std::runtime_error("Usage: interpass_experiments_timing shader-dir output.csv [scale] [comparison]");
        const int onlyScale=argc>3?std::stoi(argv[3]):0;
        const int onlyPair=argc>4?std::stoi(argv[4]):0;
        Runner r(true,32);
        auto standard=r.pipeline(argv[1],"DlssNr"),fast=r.pipeline(argv[1],"dlssnr_interpass_fast");
        auto rgb16=r.pipeline(argv[1],"dlssnr_interpass_rgb16"),rgb20=r.pipeline(argv[1],"dlssnr_interpass_rgb20");
        auto low=r.pipeline(argv[1],"dlssnr_interpass_low36");
        auto cross=r.pipeline(argv[1],"dlssnr_interpass_cross5");
        std::ofstream out(argv[2]);if(!out) throw std::runtime_error("Cannot open CSV");
        out<<"scale_percent,comparison,variant,window,sample_index,native_width,native_height,work_width,work_height,gpu_ms\n"
           <<std::setprecision(9);
        for(int percent:{33,40,41,42,49,50,59,65,90,91,95,99})
        {
            if(onlyScale && onlyScale!=percent) continue;
            const auto expected=DlssNr::InterPass::Select(3,3840,2160,
                UINT(std::lround(3840*percent/100.0)),UINT(std::lround(2160*percent/100.0)),0,1,1);
            const auto automaticFast=DlssNr::InterPass::Select(4,3840,2160,
                UINT(std::lround(3840*percent/100.0)),UINT(std::lround(2160*percent/100.0)),0,1,1);
            LowFixture fixture(r,percent,true);
            auto* rgb=expected==DlssNr::InterPass::Path::Rgb16?rgb16.Get():rgb20.Get();
            UINT window=0,pair=0;
            for(auto candidate:{automaticFast,DlssNr::InterPass::Path::FastCombinedCross5})
            {
                ++pair;
                if(onlyPair && onlyPair!=int(pair)) continue;
                if(pair==2 && onlyPair!=2) continue; // Old no-Cross5 control is opt-in fixture diagnostics only.
                const auto control=pair==1?expected:DlssNr::InterPass::Path::FastGuided;
                auto* experimental=cross.Get();
                for(auto path:{control,candidate,candidate,control})
                {
                    for(UINT warm=0;warm<90;warm+=16)
                        fixture.batch(path,standard.Get(),rgb,std::min(16u,90-warm),fast.Get(),experimental,low.Get());
                    for(UINT sample=0;sample<160;sample+=16)
                    {
                        const auto times=fixture.batch(path,standard.Get(),rgb,16,fast.Get(),experimental,low.Get());
                        for(UINT i=0;i<times.size();++i)
                            out<<percent<<','<<pair<<",\""<<DlssNr::InterPass::Name(path)<<"\","<<window<<','<<sample+i<<','
                               <<fixture.nw<<','<<fixture.nh<<','<<fixture.ww<<','<<fixture.wh<<','<<times[i]<<'\n';
                    }
                    out.flush();if(!out) throw std::runtime_error("CSV write failed");++window;
                }
            }
            std::cout<<"Synthetic experiments ABBA P"<<percent<<" complete\n"<<std::flush;
        }
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
