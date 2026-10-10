#include "pch.h"
#include "interpass_timing_fixture.h"

int main(int argc,char** argv)
{
    try {
        if(argc!=3)throw std::runtime_error("Usage: interpass_low_timing shader-dir output.csv");
        Runner runner(true,24);
        auto standard=runner.pipeline(argv[1],"DlssNr"),rgb=runner.pipeline(argv[1],"dlssnr_interpass_rgb20");
        std::ofstream out(argv[2]);if(!out)throw std::runtime_error("Cannot open CSV");
        out<<"scale_percent,variant,window,sample_index,native_width,native_height,work_width,work_height,gpu_ms\n"<<std::setprecision(9);
        for(int scale:DlssNr::kInterPassLowBenchmarkScales)
        {
            LowFixture fixture(runner,scale);UINT window=0;
            for(auto path:DlssNr::kInterPassLowBenchmarkOrder)
            {
                for(UINT warm=0;warm<90;warm+=16)fixture.batch(path,standard.Get(),rgb.Get(),std::min(16u,90-warm));
                for(UINT sample=0;sample<160;sample+=16)
                {
                    const auto times=fixture.batch(path,standard.Get(),rgb.Get(),16);
                    for(UINT i=0;i<times.size();++i)
                        out<<scale<<",\""<<DlssNr::InterPass::Name(path)<<"\","<<window<<','<<sample+i<<','
                           <<fixture.nw<<','<<fixture.nh<<','<<fixture.ww<<','<<fixture.wh<<','<<times[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");++window;
            }
            std::cout<<"Resident low-scale timing P"<<scale<<" complete\n"<<std::flush;
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
