#include "pch.h"
#include "superior_input_fixture.h"
#include "superior_numeric_metrics.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=4)throw std::runtime_error("Usage: superior_cached_low_quality baseline candidate [--4k]");
        const bool full=argc==4 && std::string(argv[3])=="--4k";
        if(argc==4 && !full)throw std::runtime_error("Unknown quality profile");
        Runner r(true,32);auto before=r.pipeline(argv[1],"dlssnr_superior_low"),after=r.pipeline(argv[2],"dlssnr_superior_low");
        unsigned checks=0;size_t total=0;SuperiorNumericMetrics metrics[2];
        const std::vector<std::pair<UINT,UINT>> sizes=full?std::vector<std::pair<UINT,UINT>>{{1920,1080},{2266,1274},{2496,1404}}:
            std::vector<std::pair<UINT,UINT>>{{1,1},{1,19},{23,1},{7,9},{16,16},{17,11},{65,37},{129,73},{257,143}};
        for(auto dims : sizes)
        for(bool halfSource : {false,true})for(bool halfTarget : {false,true})for(int stress=0;stress<4;++stress) {
            const UINT lw=(dims.first+7)/8,lh=(dims.second+7)/8;
            DlssNrConstants c{};c.Mode=18;c.Transfer=1;c.Width=lw;c.Height=lh;
            c.DirectResolveFlags=DlssNr::SuperiorScaling::ShaderFlag;
            if(!DlssNr::SuperiorScaling::LowResidual(c,dims.first,dims.second))throw std::runtime_error("Low selector failed");
            InputFixture f(r,dims.first,dims.second,lw,lh,c,stress,halfTarget,halfSource);
            auto cb=f.cb(c);
            for(UINT i=0;i<2;++i)f.table(i*16+8,f.source.Get(),f.outputs[i].reduced.Get(),f.outputs[i].reduced.Get());
            r.begin();f.roots();std::array<InputFixture::Read,2> reads;
            for(UINT i=0;i<2;++i) {
                f.dispatch(i?after.Get():before.Get(),cb.Get(),i*16+8,lw,lh,DlssNr::SuperiorScaling::LowGroupHeight);
                r.barrier(f.outputs[i].reduced.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                reads[i]=f.read(f.outputs[i].reduced.Get());
            }r.finish();
            const auto a=f.words(reads[0]),b=f.words(reads[1]);
            metrics[halfTarget].compare(a,b,halfTarget);
            total+=a.size();++checks;
        }
        for(int i=0;i<2;++i) { metrics[i].print("Cached low",i!=0);if(metrics[i].maxNormalized>(i?0.0011:0.00001))throw std::runtime_error("Low error budget exceeded"); }
        std::cout<<"Superior low GPU numerical PASS: "<<checks<<" cases, "<<total<<" words, FP16/FP32 source and target, signed HDR/nonfinite data and edges\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
