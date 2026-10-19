// Resident low + final resolve ABBA, excluding inference, DLAA, inter-pass and carrier work.
#include "pch.h"
#include "superior_resolve_fixture.h"

int main(int argc,char** argv) {
    try {
        if(argc!=4 && argc!=5)throw std::runtime_error("Usage: superior_resolve_timing baseline-dir candidate-dir output.csv [normal|normal-pair|sdr|hdr2|hdr4]");
        const std::string profile=argc==5?argv[4]:"general";
        const bool normal=argc==5,paired=normal && profile!="normal";
        const int hdr=profile.size()==4 && profile.substr(0,3)=="hdr"?profile[3]-'0':-1;
        if(normal && profile!="normal" && profile!="normal-pair" && profile!="sdr" && hdr!=2 && hdr!=4)
            throw std::runtime_error("Unknown timing profile");
        Runner r(true,64);auto before=r.pipeline(argv[1],paired?"dlssnr_superior_normal":"dlssnr_superior_resolve");
        auto after=r.pipeline(argv[2],normal?"dlssnr_superior_normal":"dlssnr_superior_resolve"),low=r.pipeline(argv[2],"DlssNr");
        if(profile=="sdr" || hdr>=0)after=r.pipeline(argv[2],std::string("dlssnr_superior_")+profile);
        auto carrier=r.pipeline(argv[2],"dlssnr_superior_carrier");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("Cannot create CSV");
        out<<"scale,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int scale : {50,59,65}) {
            ResolveFixture f(r,scale,carrier.Get(),hdr);
            // Short ABBA/BAAB cycles balance both order and nonlinear clock drift.
            for(UINT block=0;block<8;++block)for(UINT position=0;position<4;++position) {
                const UINT window=block*4+position;
                const bool candidate=(position==1 || position==2)!=(block%2!=0);
                auto* pso=candidate?after.Get():before.Get();
                for(UINT warm=0;warm<32;warm+=16)f.batch(pso,low.Get(),16);
                for(UINT sample=0;sample<20;sample+=16) {
                    auto values=f.batch(pso,low.Get(),std::min(16u,20-sample));
                    for(UINT i=0;i<values.size();++i)out<<scale<<','<<(paired?(candidate?"v51":"v50"):normal?(candidate?"v49":"v48"):(candidate?"v48":"v47"))<<','<<window<<','<<sample+i<<','<<values[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Low + resolve ABBA P"<<scale<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
