// Resident low + final resolve ABBA, excluding inference, DLAA, inter-pass and carrier work.
#include "pch.h"
#include "interpass_timing_fixture.h"

struct ResolveFixture {
    Runner& r;LowFixture images;
    ComPtr<ID3D12Resource> edit,encodeCb,decodeCb,lowCb,resolveCb;
    ResolveFixture(Runner& runner,int scale,ID3D12PipelineState* carrier):
        r(runner),images(runner,scale,true) {
        auto d=images.work->GetDesc();edit=r.resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        table(32,images.inputs[0].Get(),images.inputs[1].Get(),images.work.Get());
        table(40,images.work.Get(),images.work.Get(),edit.Get());
        table(48,images.inputs[0].Get(),edit.Get(),images.low.Get());
        table(56,images.inputs[0].Get(),edit.Get(),images.native.Get());
        DlssNrConstants c{};c.Mode=37;c.Width=images.ww;c.Height=images.wh;encodeCb=cb(c);
        c.Mode=38;decodeCb=cb(c);
        c={};c.Mode=18;c.Transfer=1;c.Width=(images.ww+7)/8;c.Height=(images.wh+7)/8;lowCb=cb(c);
        c={};c.Mode=1;c.Width=images.nw;c.Height=images.nh;c.Transfer=9;c.Passthrough=1;c.ApplyModel=1;
        c.WhitePoint=1;c.TransferStrength=1;c.ColourStrength=1;c.MaxRatio=2;c.MaxDarkening=100;
        c.ResidualHistoryValid=1;c.ResidualScale=3;c.ResidualBlend=.015f;c.ResidualConfidenceSensitivity=1;
        c.GuideWidth=1;c.MvScaleX=.8f;c.MvScaleY=.661f;c.DirectResolveFlags=65536;
        c.SkinDetail=c.SkinColour=c.EnvironmentDetail=c.EnvironmentColour=1;resolveCb=cb(c);
        r.begin();bindRoots();
        dispatch(carrier,encodeCb.Get(),32,images.ww,images.wh);
        r.barrier(images.work.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dispatch(carrier,decodeCb.Get(),40,images.ww,images.wh);
        r.barrier(edit.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        r.barrier(images.work.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        r.finish();
    }
    void table(UINT offset,ID3D12Resource* source,ID3D12Resource* model,ID3D12Resource* target) {
        auto cpu=r.heap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<6;++i) {
            auto* resource=i==1?model:i==2?images.inputs[2].Get():i==5?images.low.Get():source;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=resource->GetDesc().Format;
            d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            r.device->CreateShaderResourceView(resource,&d,{cpu.ptr+SIZE_T(offset+i)*r.stride});
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=target->GetDesc().Format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        for(UINT i=6;i<8;++i)r.device->CreateUnorderedAccessView(target,nullptr,&d,{cpu.ptr+SIZE_T(offset+i)*r.stride});
    }
    ComPtr<ID3D12Resource> cb(const DlssNrConstants& c) {
        auto b=r.buffer(sizeof(c),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* p;
        check(b->Map(0,nullptr,&p));std::memcpy(p,&c,sizeof(c));b->Unmap(0,nullptr);return b;
    }
    void bindRoots() {
        r.list->SetComputeRootSignature(r.root.Get());ID3D12DescriptorHeap* heaps[]={r.heap.Get()};r.list->SetDescriptorHeaps(1,heaps);
    }
    void dispatch(ID3D12PipelineState* pso,ID3D12Resource* constants,UINT offset,UINT w,UINT h) {
        auto gpu=r.heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(offset)*r.stride;
        r.list->SetPipelineState(pso);r.list->SetComputeRootDescriptorTable(0,gpu);
        r.list->SetComputeRootConstantBufferView(1,constants->GetGPUVirtualAddress());r.list->Dispatch((w+7)/8,(h+7)/8,1);
    }
    std::vector<double> batch(ID3D12PipelineState* resolve,ID3D12PipelineState* low,UINT count) {
        r.begin();bindRoots();
        for(UINT sample=0;sample<count;++sample) {
            r.list->EndQuery(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2);
            for(UINT repeat=0;repeat<4;++repeat) {
                dispatch(low,lowCb.Get(),48,(images.ww+7)/8,(images.wh+7)/8);
                r.barrier(images.low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                dispatch(resolve,resolveCb.Get(),56,images.nw,images.nh);
                r.barrier(images.low.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=images.native.Get();r.list->ResourceBarrier(1,&b);
            }
            r.list->EndQuery(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2+1);
        }
        r.list->ResolveQueryData(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,count*2,images.times.Get(),0);r.finish();
        UINT64* stamps;check(images.times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));std::vector<double> result;
        for(UINT i=0;i<count;++i)result.push_back(double(stamps[i*2+1]-stamps[i*2])*1000/images.frequency/4);
        images.times->Unmap(0,nullptr);return result;
    }
};
int main(int argc,char** argv) {
    try {
        if(argc!=4 && argc!=5)throw std::runtime_error("Usage: superior_resolve_timing baseline-dir candidate-dir output.csv [normal]");
        const bool normal=argc==5;
        if(normal && std::string(argv[4])!="normal")throw std::runtime_error("Unknown timing profile");
        Runner r(true,64);auto before=r.pipeline(argv[1],"dlssnr_superior_resolve");
        auto after=r.pipeline(argv[2],normal?"dlssnr_superior_normal":"dlssnr_superior_resolve"),low=r.pipeline(argv[2],"DlssNr");
        auto carrier=r.pipeline(argv[2],"dlssnr_superior_carrier");
        std::ofstream out(argv[3]);if(!out)throw std::runtime_error("Cannot create CSV");
        out<<"scale,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int scale : {50,59,65}) {
            ResolveFixture f(r,scale,carrier.Get());
            for(UINT window=0;window<4;++window) {
                const bool candidate=window==1 || window==2;auto* pso=candidate?after.Get():before.Get();
                for(UINT warm=0;warm<90;warm+=16)f.batch(pso,low.Get(),std::min(16u,90-warm));
                for(UINT sample=0;sample<160;sample+=16) {
                    auto values=f.batch(pso,low.Get(),16);
                    for(UINT i=0;i<values.size();++i)out<<scale<<','<<(normal?(candidate?"v49":"v48"):(candidate?"v48":"v47"))<<','<<window<<','<<sample+i<<','<<values[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Low + resolve ABBA P"<<scale<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
