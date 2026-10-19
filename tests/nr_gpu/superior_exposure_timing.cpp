#include "pch.h"
#include "superior_exposure_fixture.h"

int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::runtime_error("Usage: superior_exposure_timing baseline candidate output.csv normal|aa");
        const bool control=std::string(argv[4])=="aa";
        if(!control && std::string(argv[4])!="normal")throw std::runtime_error("Unknown profile");
        Runner r(true,48);auto standard=r.pipeline(argv[1],"DlssNr");
        auto first=control?standard:r.pipeline(argv[2],"dlssnr_superior_meter");
        auto second=control?standard:r.pipeline(argv[2],"dlssnr_superior_exposure");
        auto encode=r.pipeline(argv[1],"dlssnr_superior_encode"),area=r.pipeline(argv[1],"dlssnr_superior_area");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"source_format,protection,scale,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(bool halfSource : {true,false})for(float protection : {0.f,100.f})for(int scale : {50,59,65}) {
            auto c=ExposureFixture::settings(protection);
            auto selectedMeter=halfSource?first.Get():standard.Get();
            auto selectedExposure=halfSource?second.Get():standard.Get();
            ExposureFixture f(r,3840,2160,UINT(std::lround(3840*scale/100.f)),UINT(std::lround(2160*scale/100.f)),halfSource,true,0,c);
            for(UINT block=0;block<8;++block) {
                for(UINT warm=0;warm<4;++warm)f.batch(standard.Get(),standard.Get(),selectedMeter,selectedExposure,encode.Get(),area.Get(),block%2!=0,4);
                for(UINT sample=0;sample<40;sample+=4) {
                    auto values=f.batch(standard.Get(),standard.Get(),selectedMeter,selectedExposure,encode.Get(),area.Get(),block%2!=0,4);
                    for(UINT i=0;i<values.size();++i)for(UINT pos=0;pos<4;++pos) {
                        bool candidate=(pos==1||pos==2)!=(block%2!=0);
                        out<<(halfSource?"fp16":"fp32")<<','<<protection<<','<<scale<<','<<(candidate?"v55":"v54")<<','<<block*4+pos<<','<<sample+i<<','<<values[i][pos]<<'\n';
                    }
                }out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Exposure ABBA input16="<<halfSource<<" protection="<<protection<<" P"<<scale<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
