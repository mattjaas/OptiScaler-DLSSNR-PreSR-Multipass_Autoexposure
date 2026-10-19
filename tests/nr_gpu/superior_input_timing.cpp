#include "pch.h"
#include "superior_input_fixture.h"

int main(int argc,char** argv) {
    try {
        if(argc<4 || argc>6)throw std::runtime_error("Usage: superior_input_timing baseline-dir candidate-dir output.csv [normal|aa|swap] [fp16|fp32]");
        const std::string profile=argc>=5?argv[4]:"normal";const bool halfSource=argc<6 || std::string(argv[5])=="fp16";
        if(profile!="normal" && profile!="aa" && profile!="swap")throw std::runtime_error("Unknown timing profile");
        if(argc==6 && std::string(argv[5])!="fp16" && std::string(argv[5])!="fp32")throw std::runtime_error("Unknown source format");
        Runner r(true,32);auto standard=r.pipeline(argv[1],"DlssNr"),encode=profile=="aa"?standard:r.pipeline(argv[2],"dlssnr_superior_encode");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"source_format,profile,scale,stage,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(UINT pass : {0u,1u})for(int scale : {50,59,65})for(UINT kind : {0u,1u,2u}) {
            DlssNrConstants c{};c.Passthrough=pass;c.WhitePoint=2;c.PreExposure=1;c.ExposureMode=3;c.ExposureTrim=1;
            InputFixture f(r,3840,2160,UINT(std::lround(3840*scale/100.0f)),UINT(std::lround(2160*scale/100.0f)),c,0,true,halfSource);
            if(kind==1)f.seed(standard.Get());
            for(UINT block=0;block<8;++block)for(UINT position=0;position<4;++position) {
                const bool candidate=(position==1 || position==2)!=(block%2!=0);const UINT index=(candidate!=(profile=="swap"))?1:0,window=block*4+position;
                auto batch=[&](UINT n) { return f.batch(index,candidate?encode.Get():standard.Get(),standard.Get(),kind,n); };
                for(UINT warm=0;warm<32;warm+=16)batch(16);
                for(UINT sample=0;sample<20;sample+=16) {
                    const auto values=batch(std::min(16u,20-sample));
                    for(UINT i=0;i<values.size();++i)out<<(halfSource?"fp16":"fp32")<<','<<(pass?"sdr":"hdr0")<<','<<scale<<','<<kind<<','<<(candidate?"v52":"v51")<<','<<window<<','<<sample+i<<','<<values[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Input ABBA "<<(pass?"SDR":"HDR0")<<" P"<<scale<<" stage="<<kind<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
