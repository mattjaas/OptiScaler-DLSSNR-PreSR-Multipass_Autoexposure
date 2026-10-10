#include "pch.h"
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Common.h"

// Standalone DX12 fixture runner; WARP in CI, optional hardware locally.
// Checks compiled shader output, not GPU performance or temporal NGX quality.
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX12 HRESULT=" + std::to_string(hr)); }
struct Runner
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT stride;
    UINT64 serial = 0;
    HANDLE event;
    Runner(bool hardware)
    {
        ComPtr<IDXGIFactory4> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        if (hardware) check(factory->EnumAdapters1(0, &adapter));
        else check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        DXGI_ADAPTER_DESC1 ad{}; check(adapter->GetDesc1(&ad));
        std::wcout << L"Parity device: " << ad.Description << L"\n" << std::flush;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        check(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator)));
        check(device->CreateCommandList(0, q.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
        check(list->Close()); check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        event = CreateEvent(nullptr, FALSE, FALSE, nullptr); if (!event) throw std::runtime_error("CreateEvent");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 6;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 2;
        ranges[1].OffsetInDescriptorsFromTableStart = 6;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {2, ranges};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC desc{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> blob, errors;
        check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = 8;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
        stride = device->GetDescriptorHandleIncrementSize(hd.Type);
    }
    ~Runner() { CloseHandle(event); }
    ComPtr<ID3D12Resource> resource(D3D12_RESOURCE_DESC desc, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
    {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = type; hp.CreationNodeMask = hp.VisibleNodeMask = 1;
        ComPtr<ID3D12Resource> r;
        check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r)));
        return r;
    }
    ComPtr<ID3D12Resource> buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
    {
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size; d.Height = 1; d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return resource(d,type,state);
    }
    void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1,&b);
    }
    void begin() { check(allocator->Reset()); check(list->Reset(allocator.Get(),nullptr)); }
    void finish()
    {
        check(list->Close()); ID3D12CommandList* lists[] = {list.Get()}; queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.Get(),++serial));
        if (fence->GetCompletedValue() < serial) {
            check(fence->SetEventOnCompletion(serial,event));
            if (WaitForSingleObject(event,60000) != WAIT_OBJECT_0) throw std::runtime_error("GPU fence timeout");
        }
        check(device->GetDeviceRemovedReason());
    }
    ComPtr<ID3D12PipelineState> pipeline(const std::string& dir, const std::string& name)
    {
        std::ifstream f(dir + "/" + name + "_Shader.cso",std::ios::binary);
        if (!f) throw std::runtime_error("Missing shader: " + name);
        std::vector<char> code((std::istreambuf_iterator<char>(f)),{});
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature = root.Get(); d.CS = {code.data(),code.size()};
        ComPtr<ID3D12PipelineState> p; check(device->CreateComputePipelineState(&d,IID_PPV_ARGS(&p))); return p;
    }
    std::vector<uint32_t> run(ID3D12PipelineState* pso, UINT nw, UINT nh, UINT ww, UINT wh,
                              const DlssNrConstants& constants, int fixture, bool half)
    {
        begin(); std::vector<ComPtr<ID3D12Resource>> keep;
        std::mt19937 rng(1414 + fixture);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        std::array<ComPtr<ID3D12Resource>,3> inputs;
        for (int n=0; n<3; ++n) {
            UINT w=n==2?nw:ww, h=n==2?nh:wh;
            D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width=w; d.Height=h; d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            inputs[n]=resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes;
            device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
            auto upload=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            char* mapped; check(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
            for (UINT y=0;y<h;++y) for (UINT x=0;x<w;++x) {
                float v[4]; for (int c=0;c<4;++c) {
                    // Signed HDR, dark values, arbitrary alpha and finite stress cases.
                    v[c] = float(int(rng()%32769)-8192)/8192.0f;
                    if (fixture%4==1) v[c]*=0.02f;
                    if (fixture%4==2) v[c]*=64.0f;
                }
                if (fixture%4==3 && (x+y)%7==0) { uint32_t special=0x7fc00001; std::memcpy(&v[0],&special,4); }
                std::memcpy(mapped+y*fp.Footprint.RowPitch+x*16,v,16);
            }
            upload->Unmap(0,nullptr); keep.push_back(upload);
            D3D12_TEXTURE_COPY_LOCATION src{},dst{};
            src.pResource=upload.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
            dst.pResource=inputs[n].Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            barrier(inputs[n].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        for (int i=0;i<6;++i) {
            D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            v.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(inputs[i<3?i:0].Get(),&v,{cpu.ptr+SIZE_T(i)*stride});
        }
        D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=ww; d.Height=wh;
        d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1;
        d.Format=half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto target=resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto unused=resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=d.Format; u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(target.Get(),nullptr,&u,{cpu.ptr+6*stride});
        device->CreateUnorderedAccessView(unused.Get(),nullptr,&u,{cpu.ptr+7*stride});
        auto cb=buffer(sizeof(constants),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped; check(cb->Map(0,nullptr,&mapped)); std::memcpy(mapped,&constants,sizeof(constants)); cb->Unmap(0,nullptr);
        list->SetComputeRootSignature(root.Get()); ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRootConstantBufferView(1,cb->GetGPUVirtualAddress()); list->SetPipelineState(pso);
        list->Dispatch((ww+7)/8,(wh+7)/8,1);
        barrier(target.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes;
        device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto read=buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};
        src.pResource=target.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource=read.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=fp;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); finish();
        const UINT pixelWords=half?2:4;
        std::vector<uint32_t> result(ww*wh*pixelWords); check(read->Map(0,nullptr,&mapped));
        for (UINT y=0;y<wh;++y) std::memcpy(result.data()+y*ww*pixelWords,static_cast<char*>(mapped)+y*fp.Footprint.RowPitch,ww*pixelWords*4);
        read->Unmap(0,nullptr); return result;
    }
};
int main(int argc,char** argv)
{
    try {
        if (argc<2) throw std::runtime_error("Usage: v14_parity shader-dir [--hardware]");
        Runner runner(argc>2 && std::string(argv[2])=="--hardware");
        const std::string names[]={"dlssnr_tiled_v13_mode28","dlssnr_tiled_v9_linear20",
          "dlssnr_tiled_v14_rgb16","dlssnr_tiled_v14_guide16","dlssnr_tiled_v14_both16",
          "dlssnr_tiled_v14_weights20","dlssnr_tiled_v14_rgb20","dlssnr_tiled_v14_guide20","dlssnr_tiled_v14_both20"};
        std::vector<ComPtr<ID3D12PipelineState>> psos;
        for (auto& name:names) psos.push_back(runner.pipeline(argv[1],name));
        UINT64 checked=0; int fixtures=0;
        for (auto dims: {std::pair<UINT,UINT>{65,37},{127,73},{17,11}})
        for (int scale: {55,59,60,61,65,75,90,25})
        for (int stress=0;stress<4;++stress)
        for (float guide : {0.35f,1.0f})
        for (bool half : {false,true}) {
            UINT nw=dims.first, nh=dims.second, ww=std::max(1u,nw*scale/100), wh=std::max(1u,nh*scale/100);
            DlssNrConstants c{}; c.Mode=28; c.Width=ww; c.Height=wh; c.ResidualHistoryValid=1;
            c.ResidualScale=0.7f; c.ResidualBlend=stress==2?0.00001f:0.2f; c.ResidualConfidenceSensitivity=guide;
            float rx=float(ww)/nw, ry=float(wh)/nh;
            std::memcpy(&c.ResidualMotionBaseX,&rx,4); std::memcpy(&c.ResidualMotionBaseY,&ry,4);
            c.DirectResolveFlags=16|64|128|2048|16384;
            if (stress==1) { c.DirectResolveFlags|=32; c.MvScaleX=1.37f; }
            if (stress==3) { c.GuideWidth=1; c.MvScaleX=1.13f; c.MvScaleY=0.71f; }
            if (stress==2) {
                c.GuideHeight=1;
                float low=0.02f,high=0.08f,floor=0.15f;
                std::memcpy(&c.ExposureSourceWidth,&low,4); std::memcpy(&c.ExposureSourceHeight,&high,4);
                std::memcpy(&c.ExposurePadding,&floor,4);
            }
            auto ref20=runner.run(psos[1].Get(),nw,nh,ww,wh,c,stress,half);
            auto ref16=runner.run(psos[0].Get(),nw,nh,ww,wh,c,stress,half);
            for (int i=2;i<9;++i) {
                if (guide!=1.0f && (i==3 || i==4 || i==7 || i==8)) continue;
                auto actual=runner.run(psos[i].Get(),nw,nh,ww,wh,c,stress,half);
                // Scale25 forces the common Mode28 fallback; it is outside CPU
                // eligibility. Compare its v10 math to the matching v13 reference,
                // not to the older v9 fallback with different compiler branching.
                const auto& expected=(i<5 || scale==25)?ref16:ref20;
                for (size_t k=0;k<actual.size();++k) if (actual[k]!=expected[k]) {
                    // NaN payloads are not image values; alpha must otherwise match bitwise.
                    bool nanA=(actual[k]&0x7fffffff)>0x7f800000, nanB=(expected[k]&0x7fffffff)>0x7f800000;
                    if (!half && nanA && nanB) continue;
                    if (half) {
                        bool equal=true;
                        for (int shift:{0,16}) {
                            uint32_t a=(actual[k]>>shift)&0xffff, b=(expected[k]>>shift)&0xffff;
                            if (a!=b && !((a&0x7fff)>0x7c00 && (b&0x7fff)>0x7c00)) equal=false;
                        }
                        if (equal) continue;
                    }
                    std::cerr << names[i] << " mismatch scale="<<scale<<" stress="<<stress<<" component="<<k
                              <<" expected=0x"<<std::hex<<expected[k]<<" actual=0x"<<actual[k]<<std::dec<<"\n";
                    return 1;
                }
                checked+=actual.size()*(half?2:1); ++fixtures;
            }
        }
        std::cout<<"Compiled DXIL parity PASS: "<<fixtures<<" comparisons, "<<checked<<" RGBA components\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
