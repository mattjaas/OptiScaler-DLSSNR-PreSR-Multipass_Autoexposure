#pragma once
#include "DlssNr_GainPreview.h"
#include "DlssNr_GpuLifetime.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <chrono>
#include <vector>

namespace DlssNr::GainPreview
{
namespace Bytecode
{
#include "../shaders/dlssnr/superior/precompile/private_gain_preview_Shader.h"
}
// Owner serializes this object with NR's mutex. Nothing waits for GPU completion.
// Each lease has immutable descriptors/constants until its game recording cannot replay.
class Runtime
{
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Program
    {
        Ptr<ID3D12Device> device;
        Ptr<ID3D12RootSignature> root;
        Ptr<ID3D12PipelineState> pso;
    };
    struct Slot
    {
        std::shared_ptr<Program> program;
        Ptr<ID3D12Resource> image, readback, cb;
        Ptr<ID3D12DescriptorHeap> descriptors;
        Ptr<ID3D12Resource> inputs[3];
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT64 bytes = 0;
        Ptr<ID3D12CommandQueue> queue;
        Ptr<ID3D12CommandAllocator> allocator;
        Ptr<ID3D12GraphicsCommandList> copy;
        Ptr<ID3D12Fence> fence;
        uint64_t fenceValue = 0, id = 0, epoch = 0;
        const void* commands = nullptr;
        bool busy = false, retired = true, attempted = false, executed = false, consumed = false, failed = false;
        bool transformer = false;
        double time = 0;
        bool Complete() const
        {
            if (FAILED(program->device->GetDeviceRemovedReason()))
                return true;
            return executed && !failed && fence && fence->GetCompletedValue() >= fenceValue;
        }
    };
    GpuLifetime lifetime;
    std::shared_ptr<Program> program;
    std::vector<std::shared_ptr<Slot>> slots;
    std::array<float, MotionSamples * 2> previous {};
    bool previousValid = false, enabled = false, suspended = false;
    Ptr<ID3D12Device> failedDevice;
    uint64_t epoch = 1, nextId = 0, acceptedId = 0, signature = 0;
    double nextTime = 0;
    Settings settings;
    Filter filter;
    static bool Succeeded(HRESULT result) { return SUCCEEDED(result); }
    bool MakeProgram(ID3D12Device* device)
    {
        auto p = std::make_shared<Program>();
        p->device = device;
        D3D12_DESCRIPTOR_RANGE ranges[2] {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 3;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 3;
        D3D12_ROOT_PARAMETER parameters[2] {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = { 2, ranges };
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        D3D12_ROOT_SIGNATURE_DESC desc {};
        desc.NumParameters = 2;
        desc.pParameters = parameters;
        Ptr<ID3DBlob> blob, errors;
        if (!Succeeded(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
            !Succeeded(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                   IID_PPV_ARGS(&p->root))))
            return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps {};
        ps.pRootSignature = p->root.Get();
        ps.CS = { Bytecode::private_gain_preview_cso, sizeof(Bytecode::private_gain_preview_cso) };
        if (!Succeeded(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&p->pso))))
            return false;
        program = p;
        return true;
    }
    std::shared_ptr<Slot> MakeSlot()
    {
        auto s = std::make_shared<Slot>();
        s->program = program;
        auto* device = program->device.Get();
        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        hp.CreationNodeMask = hp.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = TextureWidth;
        d.Height = TextureHeight;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (!Succeeded(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                       nullptr, IID_PPV_ARGS(&s->image))))
            return {};
        device->GetCopyableFootprints(&d, 0, 1, 0, &s->footprint, nullptr, nullptr, &s->bytes);
        d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Width = s->bytes;
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        if (!Succeeded(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                                       nullptr, IID_PPV_ARGS(&s->readback))))
            return {};
        d.Width = sizeof(Constants);
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        if (!Succeeded(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                       nullptr, IID_PPV_ARGS(&s->cb))))
            return {};
        D3D12_DESCRIPTOR_HEAP_DESC heap {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 4;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!Succeeded(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&s->descriptors))))
            return {};
        slots.push_back(s);
        return s;
    }
    static void Copy(Slot& s, ID3D12CommandQueue* queue)
    {
        s.attempted = true;
        const auto type = queue->GetDesc().Type;
        if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
            s.failed = true;
            return;
        }
        auto* device = s.program->device.Get();
        if (!s.queue || s.queue.Get() != queue || !s.allocator || !s.copy || !s.fence)
        {
            s.queue = queue;
            s.allocator.Reset();
            s.copy.Reset();
            s.fence.Reset();
            s.fenceValue = 0;
            if (FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&s.allocator))) ||
                FAILED(device->CreateCommandList(0, type, s.allocator.Get(), nullptr, IID_PPV_ARGS(&s.copy))) ||
                FAILED(s.copy->Close()) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence))))
            {
                s.failed = true;
                return;
            }
        }
        if (FAILED(s.allocator->Reset()) || FAILED(s.copy->Reset(s.allocator.Get(), nullptr)))
        {
            s.failed = true;
            return;
        }
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = s.image.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = s.readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = s.footprint;
        s.copy->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (FAILED(s.copy->Close()))
        {
            s.failed = true;
            return;
        }
        ID3D12CommandList* lists[] { s.copy.Get() };
        queue->ExecuteCommandLists(1, lists);
        s.executed = true;
        s.failed = FAILED(queue->Signal(s.fence.Get(), ++s.fenceValue));
    }

  public:
    void Reset(const char* reason = "Waiting for moving frames.")
    {
        ++epoch;
        acceptedId = 0;
        previousValid = false;
        suspended = false;
        nextTime = 0;
        failedDevice.Reset();
        filter.Reset(reason);
    }
    void Configure(bool on, Settings s)
    {
        s = Sanitize(s);
        if (enabled != on || settings.requireChange != s.requireChange || settings.motionThreshold != s.motionThreshold)
            Reset(on ? "Waiting for moving frames." : "Preview disabled.");
        enabled = on;
        settings = s;
    }
    void Pause(const char* reason)
    {
        // An older asynchronous result must not unpause an unsupported/held image.
        if (!suspended)
        {
            ++epoch;
            previousValid = false;
            suspended = true;
        }
        filter.Pause(reason,
                     std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    ~Runtime()
    {
        // An unresolved private submission can outlive the last game recording.
        // As with GpuLifetime, retain its ownership rather than free resources under the GPU.
        for (auto& s : slots)
            if (s->executed && !s->Complete())
            {
                static auto* retained = new std::vector<std::shared_ptr<Slot>>;
                retained->push_back(s);
            }
    }
    void Collect()
    {
        if (!enabled && slots.empty())
        {
            program.reset();
            return;
        }
        lifetime.Collect();
        for (auto& ptr : slots)
        {
            auto& s = *ptr;
            if (!s.busy)
                continue;
            if (!s.consumed && s.Complete())
            {
                if (enabled && s.epoch == epoch && s.id > acceptedId &&
                    SUCCEEDED(s.program->device->GetDeviceRemovedReason()))
                {
                    void* p = nullptr;
                    const D3D12_RANGE range { 0, size_t(s.bytes) };
                    if (SUCCEEDED(s.readback->Map(0, &range, &p)) && p)
                    {
                        const auto reading =
                            Unpack(static_cast<char*>(p) + s.footprint.Offset, s.footprint.Footprint.RowPitch);
                        const D3D12_RANGE none { 0, 0 };
                        s.readback->Unmap(0, &none);
                        if (reading.serial == s.id)
                        {
                            acceptedId = s.id;
                            previous = reading.motion;
                            previousValid = reading.validMotion >= 548;
                            filter.Update(reading, s.transformer, settings, s.time);
                        }
                        else
                            Pause("Discarded mismatched GPU preview result.");
                    }
                    else
                        Pause("GPU preview readback failed.");
                }
                s.consumed = true;
            }
            if (s.retired && (!s.attempted || (s.failed && !s.executed)))
                s.consumed = true;
            if (s.retired && s.consumed)
            {
                s.busy = false;
                for (auto& input : s.inputs)
                    input.Reset();
            }
        }
        // Never destroy an allocator/list after an unresolved private copy submission.
        std::erase_if(slots, [&](const auto& s) { return !s->busy && (!enabled || s->program != program); });
        if (!enabled && slots.empty())
            program.reset();
    }
    bool MatchesContext(ID3D12Device* device, uint64_t context) const
    {
        return program && program->device.Get() == device && signature == context;
    }
    Status Get()
    {
        Collect();
        auto status = filter.state;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (status.valid && !status.paused && now - status.measuredAt > 1.)
        {
            status.paused = true;
            status.text = "Paused: no recent moving-frame measurement.";
        }
        return status;
    }
    bool Record(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const void* identity, ID3D12Resource* proxy,
                ID3D12Resource* answer, ID3D12Resource* motion, unsigned width, unsigned height, bool transformer,
                uint64_t context, double time)
    {
        if (!enabled)
            return false;
        Collect();
        if (suspended)
        {
            suspended = false;
            nextTime = 0;
        }
        if (failedDevice.Get() == device)
            return false;
        if (signature != context || !program || program->device.Get() != device)
        {
            Reset();
            signature = context;
            if ((!program || program->device.Get() != device) && !MakeProgram(device))
            {
                failedDevice = device;
                Pause("GPU preview shader unavailable; toggle preview to retry.");
                return false;
            }
        }
        if (time < nextTime)
            return false;
        nextTime = time + .125; // eight probes/s; no CPU/GPU waits
        if (!proxy || !answer || !motion || width < 64 || height < 36)
        {
            Pause("Preview inputs unavailable.");
            return false;
        }
        for (auto* input : { proxy, answer, motion })
            if (input->GetDesc().Width < width || input->GetDesc().Height < height)
            {
                Pause("Preview inputs smaller than the active colour grid.");
                return false;
            }
        std::shared_ptr<Slot> slot;
        unsigned currentCount = 0;
        for (auto& s : slots)
            if (s->program == program)
            {
                ++currentCount;
                if (!s->busy)
                    slot = s;
            }
        if (!slot && currentCount < 8 && slots.size() < 16)
            slot = MakeSlot();
        if (!slot)
        {
            Pause("Waiting for previous GPU recordings to retire.");
            return false;
        }
        Constants c;
        c.width = width;
        c.height = height;
        c.phaseX = nextId & 7;
        c.phaseY = (nextId >> 3) & 7;
        c.motionThreshold = settings.motionThreshold;
        c.requireChange = settings.requireChange;
        c.previousValid = previousValid;
        c.previous = previous;
        c.serialLow = uint32_t(nextId + 1);
        c.serialHigh = uint32_t((nextId + 1) >> 32);
        void* mapped = nullptr;
        if (FAILED(slot->cb->Map(0, nullptr, &mapped)))
        {
            Pause("Preview constants unavailable.");
            return false;
        }
        std::memcpy(mapped, &c, sizeof(c));
        slot->cb->Unmap(0, nullptr);
        slot->inputs[0] = proxy;
        slot->inputs[1] = answer;
        slot->inputs[2] = motion;
        auto cpu = slot->descriptors->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (unsigned i = 0; i < 3; ++i)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC d {};
            d.Format = slot->inputs[i]->GetDesc().Format;
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(slot->inputs[i].Get(), &d, { cpu.ptr + i * SIZE_T(stride) });
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
        u.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(slot->image.Get(), nullptr, &u, { cpu.ptr + 3 * SIZE_T(stride) });
        slot->busy = true;
        slot->retired = slot->attempted = slot->executed = slot->consumed = slot->failed = false;
        slot->id = ++nextId;
        slot->epoch = epoch;
        slot->time = time;
        slot->commands = identity;
        slot->transformer = transformer;
        lifetime.Record(cmd);
        // Dedicated generation: later unrelated NR recordings must not keep this slot leased.
        lifetime.Retire([slot] { slot->retired = true; });
        lifetime.BeginGeneration();
        D3D12_RESOURCE_BARRIER b {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { slot->image.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        cmd->ResourceBarrier(1, &b);
        ID3D12DescriptorHeap* heaps[] { slot->descriptors.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(program->root.Get());
        cmd->SetPipelineState(program->pso.Get());
        cmd->SetComputeRootDescriptorTable(0, slot->descriptors->GetGPUDescriptorHandleForHeapStart());
        cmd->SetComputeRootConstantBufferView(1, slot->cb->GetGPUVirtualAddress());
        cmd->Dispatch(Groups, 1, 1);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cmd->ResourceBarrier(1, &b);
        return true;
    }
    void SignalSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
    {
        if (!slots.empty())
            lifetime.Submitted(queue, count, lists);
    }
    void CopySubmitted(ID3D12CommandQueue* queue, const void* identity)
    {
        for (auto& s : slots)
            if (s->busy && !s->attempted && s->commands == identity)
                Copy(*s, queue);
    }
    void ResetRecording(ID3D12CommandList* cmd) { lifetime.ResetRecording(cmd); }
};
} // namespace DlssNr::GainPreview
