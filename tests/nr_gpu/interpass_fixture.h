#pragma once
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Common.h"

using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("DX12 HRESULT=" + std::to_string(hr)); }
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
    Runner(bool hardware, UINT descriptors = 8)
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
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = descriptors;
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
                              const DlssNrConstants& constants, int fixture, bool half, UINT groupWidth=8, UINT sourceW=0, UINT sourceH=0, double* gpuMs=nullptr)
    {
        begin(); std::vector<ComPtr<ID3D12Resource>> keep;
        std::mt19937 rng(1414 + fixture);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        std::array<ComPtr<ID3D12Resource>,3> inputs;
        for (int n=0; n<3; ++n) {
            UINT w=n==2?nw:(sourceW?sourceW:ww), h=n==2?nh:(sourceH?sourceH:wh);
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
        ComPtr<ID3D12QueryHeap> queries;
        ComPtr<ID3D12Resource> times;
        if (gpuMs) {
            D3D12_QUERY_HEAP_DESC q{}; q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; q.Count=2;
            check(device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)));
            times=buffer(16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        }
        for (int repeat=0;repeat<(gpuMs?8:1);++repeat) {
            list->Dispatch((ww+groupWidth-1)/groupWidth,(wh+7)/8,1);
            if (gpuMs) {
                D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
                b.UAV.pResource=target.Get(); list->ResourceBarrier(1,&b);
            }
        }
        if (gpuMs) {
            list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
            list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,times.Get(),0);
        }
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
        read->Unmap(0,nullptr);
        if (gpuMs) {
            UINT64 frequency; check(queue->GetTimestampFrequency(&frequency));
            UINT64* stamps; check(times->Map(0,nullptr,reinterpret_cast<void**>(&stamps)));
            *gpuMs=double(stamps[1]-stamps[0])*1000.0/double(frequency)/8.0;
            times->Unmap(0,nullptr);
        }
        return result;
    }
};
