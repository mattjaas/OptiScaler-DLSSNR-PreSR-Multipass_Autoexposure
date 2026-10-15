// Resident synthetic transitions: historical controls and v42/v43 gain-gap thresholds.
// Excludes NGX, temporal processing and final resolve; never a whole-NR measurement.
#include "pch.h"
#include "interpass_timing_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_GuidedShaping.h"

struct TransitionFixture : LowFixture {
    std::array<ComPtr<ID3D12Resource>,5> constants;
    uint32_t mask;
    TransitionFixture(Runner& r,int scale,uint32_t planned,const DlssNr::InterPass::GainSchedule& gains)
        :LowFixture(r,scale,true),mask(planned) {
        unsigned covered=0;
        for(unsigned pass=0;pass<5;++pass) {
            DlssNrConstants c{};
            if(mask & (1u<<pass)) {
                DlssNrConstants* original;check(crossCb->Map(0,nullptr,reinterpret_cast<void**>(&original)));
                c=*original;crossCb->Unmap(0,nullptr);
                const auto g=gains.Block(covered,pass+1);
                c.MvScaleX=g.high;c.MvScaleY=g.low;covered=pass+1;
            } else {c.Mode=8;c.Width=ww;c.Height=wh;}
            constants[pass]=r.buffer(sizeof(c),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            void* data;check(constants[pass]->Map(0,nullptr,&data));std::memcpy(data,&c,sizeof(c));
            constants[pass]->Unmap(0,nullptr);
        }
    }
    std::vector<double> batchTransitions(ID3D12PipelineState* standard,ID3D12PipelineState* cross,
                                        ID3D12PipelineState* cached,UINT count) {
        r.begin();r.list->SetComputeRootSignature(r.root.Get());
        ID3D12DescriptorHeap* heaps[]={r.heap.Get()};r.list->SetDescriptorHeaps(1,heaps);
        const auto bind=[&](ID3D12PipelineState* pso,ID3D12Resource* cb,UINT offset) {
            r.list->SetPipelineState(pso);
            auto gpu=r.heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(offset)*r.stride;
            r.list->SetComputeRootDescriptorTable(0,gpu);
            r.list->SetComputeRootConstantBufferView(1,cb->GetGPUVirtualAddress());
        };
        for(UINT sample=0;sample<count;++sample) {
            r.list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2);
            for(unsigned repeat=0;repeat<8;++repeat)
            for(unsigned pass=0;pass<5;++pass) {
                const bool correction=(mask & (1u<<pass))!=0;
                if(correction) {
                    bind(cached,low36Cb.Get(),24);
                    r.list->Dispatch(((ww+7)/8+7)/8,((wh+7)/8+7)/8,1);
                    r.barrier(low.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                bind(correction?cross:standard,constants[pass].Get(),0);
                r.list->Dispatch((ww+7)/8,(wh+7)/8,1);
                D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=work.Get();
                r.list->ResourceBarrier(1,&barrier);
                if(correction)r.barrier(low.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            r.list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,sample*2+1);
        }
        r.list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,count*2,times.Get(),0);
        r.finish();
        UINT64* stamps;check(times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));
        std::vector<double> result;
        for(UINT i=0;i<count;++i)result.push_back(double(stamps[i*2+1]-stamps[i*2])*1000/frequency/8);
        times->Unmap(0,nullptr);return result;
    }
};

int main(int argc,char** argv) {
    try {
        const std::string mode=argc==4?argv[3]:"";
        if((argc!=3 && argc!=4) || (argc==4 && mode!="model-intensity" && mode!="model-product" && mode!="gap-015"))
            throw std::runtime_error("Usage: interpass_gain_schedule_timing shader-dir output.csv [model-intensity|model-product|gap-015]");
        const bool modelIntensity=argc==4;
        const bool modelProduct=mode=="model-product", gap015=mode=="gap-015";
        // Historical 0.25 control only; the normal rendering path uses the production threshold.
        const auto historical=[](DlssNr::InterPass::GainSchedule plan) {
            plan.expectedMask=0;plan.expectedCovered=0;
            for(unsigned pass=0;plan.enabled && pass+1<plan.passes;++pass) {
                const auto current=plan.Block(plan.expectedCovered,pass+1);
                const auto next=plan.Block(plan.expectedCovered,pass+2);
                const double before=std::abs(double(current.high)-current.low);
                const double after=std::abs(double(next.high)-next.low);
                if(std::isfinite(before) && std::isfinite(after) && after>0.25 && after>before) {
                    plan.expectedMask|=1u<<pass;plan.expectedCovered=pass+1;
                }
            }
            return plan;
        };
        // Retained only as the historical v41 control, never used by production shaping.
        const auto additive=[](float high){return DlssNr::Profiles::StructureAdjustedHigh(high,
            std::max(0.0f,.8f-(1.0f-.7f)));};
        Runner r(true,32);
        auto standard=r.pipeline(argv[1],"DlssNr");
        auto cross=r.pipeline(argv[1],"dlssnr_interpass_cross5");
        auto cached=r.pipeline(argv[1],"dlssnr_interpass_low36_cached");
        std::ofstream out(argv[2]);if(!out)throw std::runtime_error("Cannot create CSV");
        out<<"scale,window,sample,mask,corrections,gpu_ms\n"<<std::setprecision(9);
        const std::vector<int> scales=modelIntensity?std::vector<int>{50,59,65}:std::vector<int>{50,65,84,85};
        for(int scale : scales) {
            const float exponent=std::log2(100.0f/scale);
            const auto before=historical(DlssNr::InterPass::MakeGainSchedule(6,true,true,false,[&](unsigned) {
                const float high=std::pow(.7f,exponent);
                return DlssNr::InterPass::ScheduleGains{
                    gap015?DlssNr::Profiles::ModelAdjustedHigh(high,.8f,.7f):
                    modelProduct?additive(high):modelIntensity?DlssNr::Profiles::StructureAdjustedHigh(high,.8f):high,
                    std::pow(.8f,exponent)};
            }));
            auto plan=DlssNr::InterPass::MakeGainSchedule(6,true,true,false,[&](unsigned) {
                const float high=std::pow(.7f,exponent);
                return DlssNr::InterPass::ScheduleGains{
                    (modelProduct || gap015)?DlssNr::Profiles::ModelAdjustedHigh(high,.8f,.7f):modelIntensity?additive(high):high,
                    std::pow(.8f,exponent)};
            });
            if(!gap015)plan=historical(plan);
            const unsigned stride=scale>=85?3:scale>=74?2:1;
            uint32_t legacy=0;
            for(unsigned pass=0;pass<5;++pass)if((pass+1)%stride==0)legacy|=1u<<pass;
            if(modelIntensity)legacy=before.expectedMask;
            TransitionFixture fixture(r,scale,legacy,before);
            // One resident texture set; only pre-uploaded constant buffers change per window.
            std::array<ComPtr<ID3D12Resource>,5> old=fixture.constants;
            unsigned covered=0;
            for(unsigned pass=0;pass<5;++pass) {
                DlssNrConstants c{};
                if(plan.expectedMask & (1u<<pass)) {
                    DlssNrConstants* base;check(fixture.crossCb->Map(0,nullptr,reinterpret_cast<void**>(&base)));
                    c=*base;fixture.crossCb->Unmap(0,nullptr);
                    const auto g=plan.Block(covered,pass+1);
                    c.MvScaleX=g.high;c.MvScaleY=g.low;covered=pass+1;
                } else {c.Mode=8;c.Width=fixture.ww;c.Height=fixture.wh;}
                fixture.constants[pass]=r.buffer(sizeof(c),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
                void* data;check(fixture.constants[pass]->Map(0,nullptr,&data));std::memcpy(data,&c,sizeof(c));
                fixture.constants[pass]->Unmap(0,nullptr);
            }
            const auto revised=fixture.constants;
            for(unsigned window=0;window<4;++window) {
                const bool baseline=window==0 || window==3;
                fixture.mask=baseline?legacy:plan.expectedMask;fixture.constants=baseline?old:revised;
                for(unsigned warm=0;warm<90;warm+=16)
                    fixture.batchTransitions(standard.Get(),cross.Get(),cached.Get(),std::min(16u,90-warm));
                for(unsigned sample=0;sample<160;sample+=16) {
                    const auto times=fixture.batchTransitions(standard.Get(),cross.Get(),cached.Get(),16);
                    for(unsigned i=0;i<times.size();++i)out<<scale<<','<<window<<','<<sample+i<<','<<fixture.mask<<','
                        <<DlssNr::InterPass::CorrectionCount(fixture.mask)<<','<<times[i]<<'\n';
                }
            }
            out.flush();if(!out)throw std::runtime_error("CSV write failed");
            std::cout<<"Schedule ABBA P"<<scale<<" complete\n"<<std::flush;
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
