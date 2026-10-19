#include "pch.h"
#include "superior_resolve_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::runtime_error("Usage: superior_low_timing baseline-dir candidate-dir output.csv normal|aa");
        const std::string profile=argv[4];if(profile!="normal" && profile!="aa")throw std::runtime_error("Unknown profile");
        Runner r(true,64);auto before=r.pipeline(argv[1],"DlssNr");
        auto after=profile=="aa"?before:r.pipeline(argv[2],"dlssnr_superior_low");
        auto carrier=r.pipeline(argv[2],"dlssnr_superior_carrier");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"profile,scale,stage,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int hdr : {-1,0,2,4})for(int scale : {50,59,65}) {
            ResolveFixture f(r,scale,carrier.Get(),hdr);
            DlssNrConstants c{};c.Mode=18;c.Transfer=1;c.Width=(f.images.ww+7)/8;c.Height=(f.images.wh+7)/8;
            c.DirectResolveFlags=DlssNr::SuperiorScaling::ShaderFlag;f.lowCb=f.cb(c);
            if(!DlssNr::SuperiorScaling::LowResidual(c,f.images.ww,f.images.wh))throw std::runtime_error("Low selector failed");
            auto resolve=r.pipeline(argv[2],hdr==0?"dlssnr_superior_normal":hdr<0?"dlssnr_superior_sdr":hdr==2?"dlssnr_superior_hdr2":"dlssnr_superior_hdr4");
            for(bool lowOnly : {true,false}) {
                if(lowOnly && hdr!=-1)continue; // Low has no SDR/HDR dependency; measure once.
                for(UINT block=0;block<8;++block) {
                    const UINT groupY=profile=="aa"?8u:DlssNr::SuperiorScaling::LowGroupHeight;
                    for(UINT warm=0;warm<4;++warm)f.pairedBatch(resolve.Get(),before.Get(),after.Get(),lowOnly,groupY,block%2!=0,4);
                    for(UINT sample=0;sample<40;sample+=4) {
                        auto values=f.pairedBatch(resolve.Get(),before.Get(),after.Get(),lowOnly,groupY,block%2!=0,4);
                        for(UINT i=0;i<values.size();++i)for(UINT position=0;position<4;++position) {
                            const bool candidate=(position==1 || position==2)!=(block%2!=0);
                            out<<(hdr<0?"sdr":std::string("hdr")+std::to_string(hdr))<<','<<scale<<','<<(lowOnly?"low":"chain")<<','<<(candidate?"v53":"v52")<<','<<block*4+position<<','<<sample+i<<','<<values[i][position]<<'\n';
                        }
                    }out.flush();if(!out)throw std::runtime_error("CSV write failed");
                }
                std::cout<<"Low ABBA hdr="<<hdr<<" P"<<scale<<" low-only="<<lowOnly<<" complete\n"<<std::flush;
            }
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
