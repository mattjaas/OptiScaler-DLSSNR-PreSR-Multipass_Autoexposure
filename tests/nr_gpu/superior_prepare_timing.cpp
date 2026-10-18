#include "pch.h"
#include "superior_prepare_fixture.h"

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: superior_prepare_timing baseline-dir candidate-dir output.csv");
        Runner r(true,32);auto standard=r.pipeline(argv[1],"DlssNr"),legacy=r.pipeline(argv[1],"dlssnr_superior_carrier");
        auto candidate=r.pipeline(argv[2],"dlssnr_superior_carrier");std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"scale,motion,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(bool low : {true,false})for(int scale : {50,59,65}) {
            const UINT w=UINT(std::lround(3840*scale/100.0f)),h=UINT(std::lround(2160*scale/100.0f));
            const UINT mw=low?2560:3840,mh=low?1440:2160;
            PrepareFixture f(r,w,h,2560,1440,mw,mh,{0,0,2560,1440},{0,0,mw,mh},float(w)/mw,float(h)/mh,0,true);
            for(UINT window=0;window<4;++window) {
                const bool fused=window==1 || window==2;
                for(UINT warm=0;warm<90;warm+=16)f.batch(fused,standard.Get(),legacy.Get(),candidate.Get(),std::min(16u,90-warm));
                for(UINT sample=0;sample<160;sample+=16) {
                    auto values=f.batch(fused,standard.Get(),legacy.Get(),candidate.Get(),16);
                    for(UINT i=0;i<values.size();++i)out<<scale<<','<<(low?"low":"high")<<','<<(fused?"v50":"v49")<<','<<window<<','<<sample+i<<','<<values[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Preparation ABBA P"<<scale<<" motion="<<(low?"low":"high")<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
