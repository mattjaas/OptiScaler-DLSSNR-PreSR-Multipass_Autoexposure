// Resident carrier-only ABBA. Includes the old analysis, reductions and telemetry copies;
// excludes DLAA/NGX, inference and final resolve. Allocations/uploads/fences are outside timestamps.
#include "pch.h"
#include "interpass_timing_fixture.h"

struct CarrierFixture {
    Runner& r;
    LowFixture images;
    struct Pass { ComPtr<ID3D12Resource> cb; UINT table,w,h; };
    std::vector<Pass> analysis;
    Pass encode,decode,fixedEncode,fixedDecode;
    ComPtr<ID3D12Resource> stats[2],diag[2],applied,encoded,decoded,readK,readDiag;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT kFoot{},diagFoot{};
    UINT tables=32,finalIndex=0;
    CarrierFixture(Runner& runner,int scale):r(runner),images(runner,scale) {
        const UINT w=images.ww,h=images.wh,tw=(w+31)/32,th=(h+31)/32;
        auto texture=[&](UINT x,UINT y,DXGI_FORMAT format) {
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width=x;d.Height=y;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
            d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return r.resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        };
        for(UINT i=0;i<2;++i) {
            stats[i]=texture(tw,th,DXGI_FORMAT_R32G32B32A32_FLOAT);
            diag[i]=texture(tw,th,DXGI_FORMAT_R32G32B32A32_FLOAT);
        }
        applied=texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT);
        encoded=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
        decoded=texture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
        DlssNrConstants c{};c.Mode=20;c.Width=tw;c.Height=th;c.GuideWidth=w;c.GuideHeight=h;
        c.Transfer=1;c.WhitePoint=-1;c.DebugScale=2;c.ResidualScale=.01f;
        analysis.push_back(pass(c,images.inputs[0].Get(),images.inputs[1].Get(),nullptr,stats[0].Get(),diag[0].Get()));
        UINT x=tw,y=th,index=0;
        while(x>1 || y>1) {
            c={};c.Mode=21;c.GuideWidth=x;c.GuideHeight=y;c.Width=(x+7)/8;c.Height=(y+7)/8;
            analysis.push_back(pass(c,stats[index].Get(),nullptr,nullptr,stats[1-index].Get(),nullptr));
            c.Mode=24;
            analysis.push_back(pass(c,diag[index].Get(),nullptr,nullptr,diag[1-index].Get(),nullptr));
            x=c.Width;y=c.Height;index=1-index;
        }
        finalIndex=index;
        c={};c.Mode=22;c.Width=w;c.Height=h;c.Transfer=1;c.ResidualScale=1;
        c.WhitePoint=-1;c.DebugScale=2;c.ResidualConfidenceSensitivity=32;c.TransferStrength=1;c.ColourStrength=1;
        encode=pass(c,images.inputs[0].Get(),images.inputs[1].Get(),stats[index].Get(),encoded.Get(),applied.Get());
        c.Mode=23;decode=pass(c,encoded.Get(),nullptr,applied.Get(),decoded.Get(),nullptr);
        c={};c.Mode=37;c.Width=w;c.Height=h;
        fixedEncode=pass(c,images.inputs[0].Get(),images.inputs[1].Get(),nullptr,encoded.Get(),nullptr);
        c.Mode=38;fixedDecode=pass(c,encoded.Get(),nullptr,nullptr,decoded.Get(),nullptr);
        auto readback=[&](ID3D12Resource* source,D3D12_PLACED_SUBRESOURCE_FOOTPRINT& foot) {
            auto desc=source->GetDesc();UINT64 bytes;
            r.device->GetCopyableFootprints(&desc,0,1,0,&foot,nullptr,nullptr,&bytes);
            return r.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        };
        readK=readback(applied.Get(),kFoot);readDiag=readback(diag[index].Get(),diagFoot);
    }
    Pass pass(const DlssNrConstants& c,ID3D12Resource* source,ID3D12Resource* model,
              ID3D12Resource* aux,ID3D12Resource* target,ID3D12Resource* keep) {
        Pass p{};p.w=c.Width;p.h=c.Height;p.table=tables;tables+=8;
        p.cb=r.buffer(sizeof(c),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data;check(p.cb->Map(0,nullptr,&data));std::memcpy(data,&c,sizeof(c));p.cb->Unmap(0,nullptr);
        const auto cpu=r.heap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<6;++i) {
            auto* resource=i==1 && model?model:i==5 && aux?aux:source;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=resource->GetDesc().Format;
            d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            r.device->CreateShaderResourceView(resource,&d,{cpu.ptr+SIZE_T(p.table+i)*r.stride});
        }
        for(UINT i=6;i<8;++i) {
            auto* resource=i==7 && keep?keep:target;
            D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=resource->GetDesc().Format;
            d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            r.device->CreateUnorderedAccessView(resource,nullptr,&d,{cpu.ptr+SIZE_T(p.table+i)*r.stride});
        }
        return p;
    }
    void dispatch(const Pass& p,ID3D12PipelineState* shader) {
        r.list->SetPipelineState(shader);auto gpu=r.heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr+=UINT64(p.table)*r.stride;r.list->SetComputeRootDescriptorTable(0,gpu);
        r.list->SetComputeRootConstantBufferView(1,p.cb->GetGPUVirtualAddress());
        r.list->Dispatch((p.w+7)/8,(p.h+7)/8,1);
    }
    void copy(ID3D12Resource* source,ID3D12Resource* target,const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& foot) {
        r.barrier(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=foot;
        r.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        r.barrier(source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    std::vector<double> batch(bool superior,ID3D12PipelineState* legacy,ID3D12PipelineState* fixed,UINT count) {
        r.begin();r.list->SetComputeRootSignature(r.root.Get());ID3D12DescriptorHeap* heaps[]={r.heap.Get()};
        r.list->SetDescriptorHeaps(1,heaps);
        const auto uav=D3D12_RESOURCE_STATE_UNORDERED_ACCESS,srv=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for(UINT sample=0;sample<count;++sample) {
            r.list->EndQuery(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2);
            for(UINT repeat=0;repeat<4;++repeat) {
                if(!superior) {
                    dispatch(analysis[0],legacy);UINT index=0;
                    r.barrier(stats[0].Get(),uav,srv);r.barrier(diag[0].Get(),uav,srv);
                    for(size_t j=1;j<analysis.size();j+=2) {
                        dispatch(analysis[j],legacy);dispatch(analysis[j+1],legacy);
                        r.barrier(stats[1-index].Get(),uav,srv);r.barrier(diag[1-index].Get(),uav,srv);
                        r.barrier(stats[index].Get(),srv,uav);r.barrier(diag[index].Get(),srv,uav);index=1-index;
                    }
                    dispatch(encode,legacy);r.barrier(stats[index].Get(),srv,uav);
                    r.barrier(applied.Get(),uav,srv);
                    copy(applied.Get(),readK.Get(),kFoot);copy(diag[index].Get(),readDiag.Get(),diagFoot);
                    r.barrier(diag[index].Get(),srv,uav);
                } else dispatch(fixedEncode,fixed);
                r.barrier(encoded.Get(),uav,srv);
                dispatch(superior?fixedDecode:decode,superior?fixed:legacy);
                r.barrier(encoded.Get(),srv,uav);
                if(!superior)r.barrier(applied.Get(),srv,uav);
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=decoded.Get();
                r.list->ResourceBarrier(1,&b);
            }
            r.list->EndQuery(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2+1);
        }
        r.list->ResolveQueryData(images.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,count*2,images.times.Get(),0);
        r.finish();UINT64* stamps;check(images.times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));
        std::vector<double> result;
        for(UINT i=0;i<count;++i)result.push_back(double(stamps[i*2+1]-stamps[i*2])*1000/images.frequency/4);
        images.times->Unmap(0,nullptr);return result;
    }
};

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: superior_timing shader-dir output.csv");
        Runner r(true,160);auto legacy=r.pipeline(argv[1],"DlssNr"),fixed=r.pipeline(argv[1],"dlssnr_superior_carrier");
        std::ofstream out(argv[2]);if(!out)throw std::runtime_error("Cannot create CSV");
        out<<"scale,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
        for(int scale : {50,59,65}) {
            CarrierFixture f(r,scale);
            for(UINT window=0;window<4;++window) {
                const bool superior=window==1 || window==2;
                for(UINT warm=0;warm<90;warm+=16)f.batch(superior,legacy.Get(),fixed.Get(),std::min(16u,90-warm));
                for(UINT sample=0;sample<160;sample+=16) {
                    auto values=f.batch(superior,legacy.Get(),fixed.Get(),16);
                    for(UINT i=0;i<values.size();++i)out<<scale<<','<<(superior?"Superior":"Legacy K1")<<','<<window<<','<<sample+i<<','<<values[i]<<'\n';
                }
                out.flush();if(!out)throw std::runtime_error("CSV write failed");
            }
            std::cout<<"Carrier ABBA P"<<scale<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
