#include "pch.h"
#include "superior_resolve_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

// Resident final low plus resolve; timestamps exclude NR/DLAA inference and resource initialization.
std::vector<std::array<double,4>> pairResolve(ResolveFixture& f,ID3D12PipelineState* low,
    ID3D12PipelineState* before,ID3D12PipelineState* after,bool reverse,UINT count) {
    if(count>4)throw std::runtime_error("Paired timestamp capacity exceeded");
    auto& r=f.r;r.begin();f.bindRoots();
    for(UINT sample=0;sample<count;++sample)for(UINT position=0;position<4;++position) {
        const bool candidate=(position==1 || position==2)!=reverse;
        const UINT query=(sample*4+position)*2;
        r.list->EndQuery(f.images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query);
        for(UINT repeat=0;repeat<4;++repeat) {
            f.dispatch(low,f.lowCb.Get(),48,(f.images.ww+7)/8,(f.images.wh+7)/8,DlssNr::SuperiorScaling::LowGroupHeight);
            r.barrier(f.images.low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            f.dispatch(candidate?after:before,f.resolveCb.Get(),56,f.images.nw,f.images.nh);
            r.barrier(f.images.low.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=f.images.native.Get();
            r.list->ResourceBarrier(1,&u);
        }
        r.list->EndQuery(f.images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query+1);
    }
    r.list->ResolveQueryData(f.images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,count*8,f.images.times.Get(),0);r.finish();
    UINT64* stamps;check(f.images.times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));
    std::vector<std::array<double,4>> out(count);
    for(UINT i=0;i<count;++i)for(UINT j=0;j<4;++j) {
        const UINT q=(i*4+j)*2;out[i][j]=double(stamps[q+1]-stamps[q])*1000/f.images.frequency/4;
    }
    f.images.times->Unmap(0,nullptr);return out;
}
int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::runtime_error("Usage: superior_softknee_timing baseline-dir candidate-dir output.csv normal|aa pilot|full");
        const std::string profile=argv[4];if(profile!="normal" && profile!="aa")throw std::runtime_error("Unknown profile");
        const std::string kind="resolve", length=argv[5]; const bool pilot=length=="pilot";
        if(length!="pilot" && length!="full")throw std::runtime_error("Unknown run length");
        Runner r(true,64);
        auto carrier=r.pipeline(argv[1],"dlssnr_superior_carrier");
        auto low=r.pipeline(argv[1],"dlssnr_superior_low");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"profile,scale,stage,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int hdr : {0})for(int scale : {50,59,65}) {
            ResolveFixture f(r,scale,carrier.Get(),hdr);
            DlssNrConstants c{};c.Mode=18;c.Transfer=1;c.Width=(f.images.ww+7)/8;c.Height=(f.images.wh+7)/8;
            c.DirectResolveFlags=DlssNr::SuperiorScaling::ShaderFlag;f.lowCb=f.cb(c);
            if(!DlssNr::SuperiorScaling::LowResidual(c,f.images.ww,f.images.wh))throw std::runtime_error("Low selector failed");
            auto before=r.pipeline(argv[1],"dlssnr_superior_normal");
            auto after=profile=="aa"?before:r.pipeline(argv[2],"dlssnr_superior_hdr0");
            for(UINT block=0;block<(pilot?2u:8u);++block) {
                for(UINT warm=0;warm<4;++warm)pairResolve(f,low.Get(),before.Get(),after.Get(),block%2!=0,4);
                for(UINT sample=0;sample<(pilot?20u:40u);sample+=4) {
                    auto values=pairResolve(f,low.Get(),before.Get(),after.Get(),block%2!=0,4);
                    for(UINT i=0;i<values.size();++i)for(UINT position=0;position<4;++position) {
                        const bool candidate=(position==1 || position==2)!=(block%2!=0);
                        out<<(hdr<0?"sdr":std::string("hdr")+std::to_string(hdr))<<','<<scale<<','<<kind<<','<<(candidate?"candidate":"base")<<','<<block*4+position<<','<<sample+i<<','<<values[i][position]<<'\n';
                    }
                }out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Resolve ABBA hdr="<<hdr<<" P"<<scale<<" stage="<<kind<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
