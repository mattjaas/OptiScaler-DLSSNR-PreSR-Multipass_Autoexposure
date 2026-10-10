#include "pch.h"
#include "interpass_timing_fixture.h"

// Resident synthetic 4K shader A/B/B/A. This excludes NR/NGX, temporal DLAA and final resolve.
// In-game FastQuality is the production measurement; this fixture only tests the cost hypothesis.
int main(int argc, char** argv)
{
    try
    {
        if(argc!=3) throw std::runtime_error("Usage: interpass_fast_timing shader-dir output.csv");
        Runner runner(true,24);
        auto standard=runner.pipeline(argv[1],"DlssNr");
        auto rgb16=runner.pipeline(argv[1],"dlssnr_interpass_rgb16");
        auto rgb20=runner.pipeline(argv[1],"dlssnr_interpass_rgb20");
        auto fast=runner.pipeline(argv[1],"dlssnr_interpass_fast");
        std::ofstream out(argv[2]); if(!out) throw std::runtime_error("Cannot open CSV");
        out<<"scale_percent,variant,window,sample_index,native_width,native_height,work_width,work_height,gpu_ms\n"
           <<std::setprecision(9);
        for(const auto& scale:DlssNr::kInterPassBenchmarkScales)
        {
            LowFixture fixture(runner,scale.percent);
            auto* rgb=scale.expected==DlssNr::InterPass::Path::Rgb16?rgb16.Get():rgb20.Get();
            UINT window=0;
            for(auto path:{scale.expected,DlssNr::InterPass::Path::FastGuided,
                           DlssNr::InterPass::Path::FastGuided,scale.expected})
            {
                for(UINT warm=0;warm<90;warm+=16)
                    fixture.batch(path,standard.Get(),rgb,std::min(16u,90-warm),fast.Get());
                for(UINT sample=0;sample<160;sample+=16)
                {
                    const auto times=fixture.batch(path,standard.Get(),rgb,16,fast.Get());
                    for(UINT i=0;i<times.size();++i)
                        out<<scale.percent<<",\""<<DlssNr::InterPass::Name(path)<<"\","<<window<<','<<sample+i<<','
                           <<fixture.nw<<','<<fixture.nh<<','<<fixture.ww<<','<<fixture.wh<<','<<times[i]<<'\n';
                }
                out.flush(); if(!out) throw std::runtime_error("CSV write failed"); ++window;
            }
            std::cout<<"Synthetic Fast ABBA P"<<scale.percent<<" complete\n"<<std::flush;
        }
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
