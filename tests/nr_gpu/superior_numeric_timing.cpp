#include "pch.h"
#include "superior_resolve_fixture.h"
#include "superior_input_fixture.h"
#include <memory>
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

// Both modified portions run on resident resources. NR/DLAA inference is intentionally absent.
std::vector<std::array<double,4>> pairResolve(ResolveFixture& f,ID3D12PipelineState* low,
    ID3D12PipelineState* before,ID3D12PipelineState* after,bool reverse,UINT count,
    InputFixture* input,ID3D12PipelineState* encode,ID3D12PipelineState* areaA,ID3D12PipelineState* areaB) {
    if(count>4)throw std::runtime_error("Paired timestamp capacity exceeded");
    auto& r=f.r;r.begin();f.bindRoots();
    for(UINT sample=0;sample<count;++sample)for(UINT position=0;position<4;++position) {
        const bool candidate=(position==1 || position==2)!=reverse;
        const UINT query=(sample*4+position)*2;
        r.list->EndQuery(f.images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query);
        for(UINT repeat=0;repeat<4;++repeat) {
            if(input)input->prepare(0,encode,candidate?areaB:areaA,2);
            f.dispatch(low,f.lowCb.Get(),48,(f.images.ww+7)/8,(f.images.wh+7)/8,4u); // Historical v53/v54 comparison uses the reference low PSO (8x4).
            r.barrier(f.images.low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            f.dispatch(candidate?after:before,f.resolveCb.Get(),56,f.images.nw,f.images.nh);
            r.barrier(f.images.low.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=f.images.native.Get();
            r.list->ResourceBarrier(1,&u);
            if(input)input->rest(0,2);
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
        if(argc!=5 && argc!=6)throw std::runtime_error("Usage: superior_numeric_timing baseline-dir candidate-dir output.csv normal|aa [resolve|combined]");
        const std::string profile=argv[4];if(profile!="normal" && profile!="aa")throw std::runtime_error("Unknown profile");
        const std::string kind=argc==6?argv[5]:"resolve";
        if(kind!="resolve" && kind!="combined")throw std::runtime_error("Unknown stage");
        Runner r(true,64);
        auto carrier=r.pipeline(argv[1],"dlssnr_superior_carrier");
        auto low=r.pipeline(argv[1],"dlssnr_superior_low"),encode=r.pipeline(argv[1],"dlssnr_superior_encode");
        auto areaA=r.pipeline(argv[1],"DlssNr"),areaB=profile=="aa"?areaA:r.pipeline(argv[2],"dlssnr_superior_area");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
        out<<"profile,scale,stage,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int hdr : {-1,0,2,4})for(int scale : {50,59,65}) {
            ResolveFixture f(r,scale,carrier.Get(),hdr);
            DlssNrConstants c{};c.Mode=18;c.Transfer=1;c.Width=(f.images.ww+7)/8;c.Height=(f.images.wh+7)/8;
            c.DirectResolveFlags=DlssNr::SuperiorScaling::ShaderFlag;f.lowCb=f.cb(c);
            if(!DlssNr::SuperiorScaling::LowResidual(c,f.images.ww,f.images.wh))throw std::runtime_error("Low selector failed");
            const std::string name=hdr==0?"dlssnr_superior_normal":hdr<0?"dlssnr_superior_sdr":hdr==2?"dlssnr_superior_hdr2":"dlssnr_superior_hdr4";
            auto before=r.pipeline(argv[1],name),after=profile=="aa"?before:r.pipeline(argv[2],name);
            std::unique_ptr<InputFixture> input;
            if(kind=="combined") {
                DlssNrConstants in{};in.Passthrough=hdr<0?1u:0u;in.ReversibleMode=hdr<0?0u:UINT(hdr);
                in.WhitePoint=2;in.PreExposure=1;in.ExposureMode=3;in.ExposureTrim=1;
                // Input uses descriptors 0..31; final low/resolve uses 48..63 after initialization.
                input=std::make_unique<InputFixture>(r,f.images.nw,f.images.nh,f.images.ww,f.images.wh,in,0,true,true);
                DlssNrConstants down{};down.Mode=2;down.Width=f.images.ww;down.Height=f.images.wh;
                down.DirectResolveFlags=DlssNr::SuperiorScaling::ShaderFlag;input->areaCb=input->cb(down);
                if(!DlssNr::SuperiorScaling::AreaDownsample(down,f.images.nw,f.images.nh))throw std::runtime_error("Area selector failed");
            }
            for(UINT block=0;block<8;++block) {
                for(UINT warm=0;warm<4;++warm)pairResolve(f,low.Get(),before.Get(),after.Get(),block%2!=0,4,input.get(),encode.Get(),areaA.Get(),areaB.Get());
                for(UINT sample=0;sample<40;sample+=4) {
                    auto values=pairResolve(f,low.Get(),before.Get(),after.Get(),block%2!=0,4,input.get(),encode.Get(),areaA.Get(),areaB.Get());
                    for(UINT i=0;i<values.size();++i)for(UINT position=0;position<4;++position) {
                        const bool candidate=(position==1 || position==2)!=(block%2!=0);
                        out<<(hdr<0?"sdr":std::string("hdr")+std::to_string(hdr))<<','<<scale<<','<<kind<<','<<(candidate?"v54":"v53")<<','<<block*4+position<<','<<sample+i<<','<<values[i][position]<<'\n';
                    }
                }out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Resolve ABBA hdr="<<hdr<<" P"<<scale<<" stage="<<kind<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
