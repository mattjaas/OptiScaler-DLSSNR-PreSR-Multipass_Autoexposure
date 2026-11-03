// Independent reference for the optional decode PSO; included after Fixture.
void GhostQuality(Fixture& f)
{
    using namespace DlssNr::Superior::Fields;
    auto ps = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_ghost");
    unsigned cases = 0;
    for (bool tiny : { false, true }) for (bool full : { false, true }) for(bool half : {false,true})
    for (float gain : { .5f, 1.f, 6.f }) for (bool transforms : { false, true })
    for (float strength : { 0.f, .5f, 1.f }) for (float fade : { 0.f, .5f, 1.f })
    {
        const unsigned w = tiny ? 1 : 17, h = tiny ? 1 : 9;
        std::vector<float> proxy(w*h*4,.5f), answer(proxy), filtered(proxy), expected(proxy), depth(proxy);
        std::vector<double> raw(proxy.size()), wanted(proxy.size());
        const double sat = transforms ? 3 : 1, spatialGain = transforms ? 1 + .5*(std::max(1.f,gain)-1) : gain;
        const double colour = transforms ? .0625 : 0, bias = transforms ? .05 : 0;
        for (unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
        {
            const size_t i=(y*w+x)*4;
            double amplified[3];
            for(unsigned ch=0;ch<3;++ch)
            {
                // Isolated edges, near-neutral patches, both signs and RGB-only excursions.
                const double edit = ((x+2*y+ch)%5==0 ? .04 : -.01) + .001*ch;
                proxy[i+ch]=.375f+.0625f*((x+y+ch)%5);
                answer[i+ch]=float(proxy[i+ch]+edit); raw[i+ch]=double(answer[i+ch])-proxy[i+ch];
                wanted[i+ch]=raw[i+ch]+((x+y+ch)%3==0 ? .09 : (x+y+ch)%3==1 ? -.05 : .005);
                amplified[ch]=wanted[i+ch]>0 ? wanted[i+ch]*spatialGain : wanted[i+ch];
            }
            const double grey=.25*amplified[0]+.5*amplified[1]+.25*amplified[2];
            for(unsigned ch=0;ch<3;++ch)
                filtered[i+ch]=float((full?proxy[i+ch]:.5)+grey+sat*(amplified[ch]-grey)+colour+bias);
            // A bad NR sample must use the same finite proxy fallback as the production path.
            if (!tiny && x==0 && y==0) { answer[i]=NAN; raw[i]=0; }
        }
        // Reference decode includes the stored FP32 input, so its small rounding errors are explicit.
        for(size_t i=0;i<filtered.size();i+=4)
        {
            double v[3], grey=0;
            for(unsigned ch=0;ch<3;++ch) v[ch]=double(filtered[i+ch])-(full?proxy[i+ch]:.5)-colour-bias;
            grey=.25*v[0]+.5*v[1]+.25*v[2];
            for(unsigned ch=0;ch<3;++ch)
            {
                const double restored=grey+(v[ch]-grey)/sat;
                wanted[i+ch]=restored>0 ? restored/spatialGain : restored;
            }
        }
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
        {
            const size_t i=(y*w+x)*4;
            double distance=0;
            for(unsigned ch=0;ch<3;++ch)
            {
                double low=raw[i+ch],high=low;
                for(int oy=-1;oy<=1;++oy) for(int ox=-1;ox<=1;++ox)
                {
                    const int xx=std::clamp(int(x)+ox,0,int(w)-1), yy=std::clamp(int(y)+oy,0,int(h)-1);
                    const auto value=raw[(yy*w+xx)*4+ch]; low=std::min(low,value);high=std::max(high,value);
                }
                distance=std::max(distance,std::max(low-wanted[i+ch],wanted[i+ch]-high));
            }
            const double t=std::clamp((distance-.01)/.02,0.,1.), mask=strength*t*t*(3-2*t);
            for(unsigned ch=0;ch<3;++ch)
                expected[i+ch]=float((wanted[i+ch]*(1-mask)+raw[i+ch]*mask)*(1-fade)+raw[i+ch]*fade);
        }
        f.r.begin();
        auto p=f.texture(w,h,&proxy), a=f.texture(w,h,&answer), s=f.texture(w,h,&filtered),
             d=f.texture(w,h,&depth,DXGI_FORMAT_R32_FLOAT), out=f.texture(w,h,nullptr,half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
        DlssNr::Superior::DlssNrConstants c{};c.Mode=38;c.Width=w;c.Height=h;
        SetPositiveCarrierGain(c,gain);SetCarrierSaturation(c,float(sat));SetCarrierNrAnswer(c,full);
        SetCarrierGhostRejection(c,{true,.01f,.02f,strength});SetUnfilteredEditWeight(c,fade);
        if(transforms)
        {
            SetCarrierDepthColour(c,.125f,1.f,0,false);
            DlssNr::DepthTransport::Settings transport;
            transport.weightedGain=true;transport.additive=true;transport.fullRange=true;
            transport.curve=1;transport.bias=.1f;
            SetCarrierDepthBrightness(c,transport);
        }
        f.dispatch(ps.Get(),c,s.Get(),p.Get(),out.Get(),(w+7)/8,(h+7)/8,d.Get(),p.Get(),a.Get());
        const auto got=f.read(out.Get());
        for(size_t i=0;i<got.size();++i)
            if(i%4!=3) Near(got[i],expected[i],("ghost rejection independent RGB reference: case="+std::to_string(cases)+
                " index="+std::to_string(i)+" gain="+std::to_string(gain)+" full="+std::to_string(full)+
                " transforms="+std::to_string(transforms)+" strength="+std::to_string(strength)+" fade="+std::to_string(fade)).c_str());
        ++cases;
        if(tiny && strength==1.f && fade==0.f)
        {
            // Bypass host sanitation to verify the shader's independent fallback too.
            c.ColourStrength=NAN;c.TransferStrength=-1.f;c.CompareSplit=INFINITY;
            f.r.begin();auto fallback=f.texture(w,h);
            f.dispatch(ps.Get(),c,s.Get(),p.Get(),fallback.Get(),1,1,d.Get(),p.Get(),a.Get());
            const auto values=f.read(fallback.Get());
            for(unsigned ch=0;ch<3;++ch)Near(values[ch],expected[ch],"Ghost shader invalid-parameter fallback");
            ++cases;
        }
    }
    DlssNr::Superior::DlssNrConstants c{};c.Mode=38;
    SetCarrierGhostRejection(c,{true,NAN,INFINITY,NAN});
    if(c.ColourStrength!=.01f || c.TransferStrength!=.02f || c.CompareSplit!=1 || !CarrierGhostRejection(c))
        throw std::runtime_error("Ghost settings finite fallback/ABI");
    SetCarrierGhostRejection(c,{true,.01f,.02f,0});
    if(CarrierGhostRejection(c)) throw std::runtime_error("Zero strength must select the ordinary PSO");
    std::cout<<"Ghost rejection GPU PASS: "<<cases<<" independent-reference cases\n";
}

void GhostTiming(Fixture& f)
{
    auto& r=f.r;
    auto ghost=r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile","private_carrier_ghost");
    std::ofstream csv("artifacts/validation/private-ghost-timing.csv");
    csv<<"width,height,block,position,variant,ms\n";
    for(auto size:{std::pair<UINT,UINT>{2496,1404},{3840,2160}})
    {
        const auto w=size.first,h=size.second;
        std::vector<float> proxy(size_t(w)*h*4,.5f), answer(proxy), filtered(proxy);
        for(size_t i=0;i<proxy.size();++i)
        {answer[i]+=.02f*std::sin(float(i%997)*.013f);filtered[i]+=.05f*std::sin(float(i%993)*.017f);}
        // texture() uploads FP32, so prepare genuine FP16 inputs through the production encoder.
        r.begin();
        auto pi=f.texture(w,h,&proxy), ai=f.texture(w,h,&answer), si=f.texture(w,h,&filtered);
        auto pp=f.texture(w,h,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT),
             aa=f.texture(w,h,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT),
             ss=f.texture(w,h,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT),
             out=f.texture(w,h,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
        DlssNr::Superior::DlssNrConstants c{};c.Mode=37;c.Width=w;c.Height=h;c.WhitePoint=1;c.MaxRatio=1;
        f.dispatch(f.carrier.Get(),c,pi.Get(),pi.Get(),pp.Get(),(w+7)/8,(h+7)/8);
        f.dispatch(f.carrier.Get(),c,pi.Get(),ai.Get(),aa.Get(),(w+7)/8,(h+7)/8);
        f.dispatch(f.carrier.Get(),c,pi.Get(),si.Get(),ss.Get(),(w+7)/8,(h+7)/8);
        r.barrier(pp.Get(),UAV,SRV);r.barrier(aa.Get(),UAV,SRV);r.barrier(ss.Get(),UAV,SRV);
        r.finish();
        auto cpu=r.heap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<8;++i)
        {
            if(i<6)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
                d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
                r.device->CreateShaderResourceView(i==2?pp.Get():i==4?aa.Get():ss.Get(),&d,{cpu.ptr+SIZE_T(i)*r.stride});
            }
            else
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
                r.device->CreateUnorderedAccessView(out.Get(),nullptr,&d,{cpu.ptr+SIZE_T(i)*r.stride});
            }
        }
        c.Mode=38;DlssNr::Superior::Fields::SetCarrierGhostRejection(c,{true,.01f,.02f,1});
        auto cb=r.buffer(256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data;check(cb->Map(0,nullptr,&data));std::memcpy(data,&c,256);cb->Unmap(0,nullptr);
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
        ComPtr<ID3D12QueryHeap> queries;check(r.device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)));
        auto times=r.buffer(16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        UINT64 frequency;check(r.queue->GetTimestampFrequency(&frequency));
        auto measure=[&](bool candidate)
        {
            r.begin();r.list->SetComputeRootSignature(r.root.Get());ID3D12DescriptorHeap* heaps[]{r.heap.Get()};r.list->SetDescriptorHeaps(1,heaps);
            r.list->SetComputeRootDescriptorTable(0,r.heap->GetGPUDescriptorHandleForHeapStart());r.list->SetComputeRootConstantBufferView(1,cb->GetGPUVirtualAddress());
            r.list->SetPipelineState(candidate?ghost.Get():f.carrier.Get());
            r.list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
            r.list->Dispatch((w+7)/8,(h+7)/8,1);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=out.Get();r.list->ResourceBarrier(1,&b);
            r.list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);r.list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,times.Get(),0);r.finish();
            UINT64* values;check(times->Map(0,nullptr,reinterpret_cast<void**>(&values)));
            const double ms=double(values[1]-values[0])*1000/frequency;times->Unmap(0,nullptr);return ms;
        };
        for(unsigned i=0;i<16;++i){measure(false);measure(true);}
        for(unsigned block=0;block<16;++block)
        {
            unsigned position=0;
            for(bool candidate:{false,true,true,false})
                csv<<w<<','<<h<<','<<block<<','<<position++<<','<<(candidate?'B':'A')<<','<<measure(candidate)<<'\n';
        }
    }
    std::cout<<"Synthetic decode ABBA timings: artifacts/validation/private-ghost-timing.csv\n";
}
