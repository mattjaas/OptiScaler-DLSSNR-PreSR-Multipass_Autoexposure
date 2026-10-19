#pragma once
#include "superior_input_fixture.h"
#include "superior_numeric_metrics.h"

// One physical target for A/B. Restore the game's original outside each timestamp.
// Model inference is replaced by a resident, deterministic signed residual.
struct HdrCopyFixture {
    Runner& r;
    InputFixture input, residual;
    ComPtr<ID3D12Resource> target, hdr, proxy, workProxy, low, encodeCb, areaCb, lowCb, resolveCb;
    UINT nw,nh,ww,wh;

    HdrCopyFixture(Runner& runner,UINT width,UINT height,UINT workW,UINT workH,
                   bool half,int pattern,const DlssNrConstants& settings):
        r(runner),input(runner,width,height,workW,workH,settings,std::min(pattern,3),half,half),
        residual(runner,workW,workH,(workW+7)/8,(workH+7)/8,settings,0,true,true),
        nw(width),nh(height),ww(workW),wh(workH) {
        target=input.outputs[1].keep;hdr=input.outputs[0].keep;
        proxy=input.outputs[0].color;workProxy=input.outputs[0].reduced;low=residual.outputs[0].reduced;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{target->GetDesc().Format};
        check(r.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)));
        if(!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))throw std::runtime_error("Typed UAV load unavailable");
        if(pattern==4)specialSource(half);
        // Unused tables/resources from the common fixtures are not part of the measured path.
        input.outputs[1].color.Reset();input.outputs[1].reduced.Reset();
        for(UINT i=0;i<2;++i) { residual.outputs[i].color.Reset();residual.outputs[i].keep.Reset(); }
        residual.outputs[1].reduced.Reset();
        table(0,target.Get(),input.source.Get(),input.source.Get(),proxy.Get(),hdr.Get());
        table(8,proxy.Get(),input.source.Get(),input.source.Get(),workProxy.Get(),workProxy.Get());
        table(16,workProxy.Get(),residual.source.Get(),input.source.Get(),low.Get(),low.Get());
        table(24,workProxy.Get(),residual.source.Get(),hdr.Get(),target.Get(),target.Get());
        // Never alias an SRV of target with its UAV. The in-place PSO does not read t2.
        table(32,workProxy.Get(),residual.source.Get(),input.source.Get(),target.Get(),target.Get());
        auto c=settings;c.Mode=0;c.Width=nw;c.Height=nh;encodeCb=input.cb(c);
        c={};c.Mode=2;c.Width=ww;c.Height=wh;c.DirectResolveFlags=65536;areaCb=input.cb(c);
        c={};c.Mode=18;c.Transfer=1;c.Width=(ww+7)/8;c.Height=(wh+7)/8;c.DirectResolveFlags=65536;lowCb=input.cb(c);
        c=settings;c.Mode=1;c.Width=nw;c.Height=nh;c.Transfer=9;c.ApplyModel=1;
        c.ResidualHistoryValid=1;c.ResidualScale=3;c.ResidualBlend=.015f;c.ResidualConfidenceSensitivity=1;
        c.GuideWidth=1;c.ModelWorkScale=float(ww)/nw;c.DirectResolveFlags=65536;
        c.MaxRatio=2;c.MaxDarkening=100;c.SkinDetail=c.SkinColour=c.EnvironmentDetail=c.EnvironmentColour=1;
        resolveCb=input.cb(c);
    }
    void table(UINT offset,ID3D12Resource* a,ID3D12Resource* b,ID3D12Resource* original,
               ID3D12Resource* out,ID3D12Resource* keep) {
        const auto cpu=r.heap->GetCPUDescriptorHandleForHeapStart();
        ID3D12Resource* srv[]={a,b,original,input.exposure.Get(),input.source.Get(),low.Get()};
        for(UINT i=0;i<6;++i) {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=srv[i]->GetDesc().Format;
            d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            r.device->CreateShaderResourceView(srv[i],&d,{cpu.ptr+SIZE_T(offset+i)*r.stride});
        }
        for(UINT i=0;i<2;++i) {
            auto* resource=i?keep:out;D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
            d.Format=resource->GetDesc().Format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            r.device->CreateUnorderedAccessView(resource,nullptr,&d,{cpu.ptr+SIZE_T(offset+6+i)*r.stride});
        }
    }
    void specialSource(bool half) {
        auto d=input.source->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        r.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto upload=r.buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);char* p;
        check(upload->Map(0,nullptr,reinterpret_cast<void**>(&p)));
        const uint16_t h[]={0,0x8000,1,0x8001,0x3c00,0xbc00,0x7bff,0xfbff,0x7c00,0xfc00,0x7c01,0x7e55};
        const uint32_t f[]={0,0x80000000,1,0x80000001,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0x7fc12345};
        for(UINT y=0;y<nh;++y)for(UINT x=0;x<nw;++x)for(UINT k=0;k<4;++k) {
            const auto i=(x*3+y*5+k)%12;
            if(half)std::memcpy(p+y*fp.Footprint.RowPitch+(x*4+k)*2,&h[i],2);
            else std::memcpy(p+y*fp.Footprint.RowPitch+(x*4+k)*4,&f[i],4);
        }
        upload->Unmap(0,nullptr);r.begin();
        r.barrier(input.source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=upload.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;
        b.pResource=input.source.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;r.list->CopyTextureRegion(&b,0,0,0,&a,nullptr);
        r.barrier(input.source.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);r.finish();
    }
    void seed() {
        r.barrier(input.source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        r.barrier(target.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        r.list->CopyResource(target.Get(),input.source.Get());
        r.barrier(target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        r.barrier(input.source.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    using Reads=std::array<InputFixture::Read,3>;
    void run(ID3D12PipelineState* encode,ID3D12PipelineState* area,ID3D12PipelineState* lowPso,
             ID3D12PipelineState* resolve,bool inplace,Reads* reads=nullptr) {
        input.dispatch(encode,encodeCb.Get(),0,nw,nh);
        r.barrier(target.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        for(auto* p : {proxy.Get(),hdr.Get()})r.barrier(p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input.dispatch(area,areaCb.Get(),8,ww,wh);
        r.barrier(workProxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(reads) { (*reads)[0]=input.read(proxy.Get());(*reads)[1]=input.read(workProxy.Get()); }
        input.dispatch(lowPso,lowCb.Get(),16,(ww+7)/8,(wh+7)/8,4);
        r.barrier(low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        input.dispatch(resolve,resolveCb.Get(),inplace?32:24,nw,nh);
        if(reads) {
            r.barrier(target.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            (*reads)[2]=input.read(target.Get());
            r.barrier(target.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        for(auto* p : {proxy.Get(),hdr.Get(),workProxy.Get(),low.Get()})r.barrier(p,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    size_t quality(ID3D12PipelineState* encodeA,ID3D12PipelineState* encodeB,ID3D12PipelineState* area,
                   ID3D12PipelineState* lowPso,ID3D12PipelineState* resolveA,ID3D12PipelineState* resolveB) {
        Reads a,b;r.begin();input.roots();seed();run(encodeA,area,lowPso,resolveA,false,&a);r.finish();
        r.begin();input.roots();seed();run(encodeB,area,lowPso,resolveB,true,&b);r.finish();
        size_t words=0;
        for(UINT i=0;i<3;++i) {
            const auto av=input.words(a[i]),bv=input.words(b[i]);
            if(av!=bv) {
                for(size_t j=0;j<av.size();++j)if(av[j]!=bv[j]) {
                    std::cerr<<"HDR copy mismatch field="<<i<<" word="<<j<<" before="<<std::hex<<av[j]<<" after="<<bv[j]<<std::dec<<'\n';break;
                }
                throw std::runtime_error("HDR copy removal is not bit-exact");
            }words+=av.size();
        }return words;
    }
    std::vector<std::array<double,4>> batch(ID3D12PipelineState* encodeA,ID3D12PipelineState* encodeB,
        ID3D12PipelineState* area,ID3D12PipelineState* lowPso,ID3D12PipelineState* resolveA,
        ID3D12PipelineState* resolveB,bool control,bool reverse,UINT count) {
        if(count>4)throw std::runtime_error("Timestamp capacity exceeded");
        r.begin();input.roots();
        for(UINT sample=0;sample<count;++sample)for(UINT pos=0;pos<4;++pos) {
            const bool candidate=!control && ((pos==1 || pos==2)!=reverse);
            const UINT q=(sample*4+pos)*2;
            seed(); // Same physical target, original restoration outside the measured interval.
            r.list->EndQuery(input.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,q);
            run(candidate?encodeB:encodeA,area,lowPso,candidate?resolveB:resolveA,candidate);
            r.list->EndQuery(input.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,q+1);
        }
        r.list->ResolveQueryData(input.queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,count*8,input.times.Get(),0);r.finish();
        UINT64* stamps;check(input.times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));
        std::vector<std::array<double,4>> values(count);
        for(UINT i=0;i<count;++i)for(UINT pos=0;pos<4;++pos) {
            const UINT q=(i*4+pos)*2;values[i][pos]=double(stamps[q+1]-stamps[q])*1000/input.frequency;
        }
        input.times->Unmap(0,nullptr);return values;
    }
};
