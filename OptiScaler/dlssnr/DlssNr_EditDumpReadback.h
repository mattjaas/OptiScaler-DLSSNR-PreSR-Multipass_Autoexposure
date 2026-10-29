#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace DlssNr::EditDump
{
// A capture-owned copy, submitted once AFTER the game's snapshot-producing list on its actual queue.
// Its readback is never written by the replayable game list. The owner's job must retain this
// object while Pending (including a failed queue Signal), or until device removal.
class ReadbackCopy
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    Microsoft::WRL::ComPtr<ID3D12Resource> source, destination;
    bool attempted = false, executed = false;
    HRESULT result = S_OK;

  public:
    enum class State
    {
        NotSubmitted,
        Pending,
        Complete,
        Failed,
        DeviceRemoved
    };
    HRESULT Submit(ID3D12CommandQueue* actualQueue, ID3D12Resource* snapshot, ID3D12Resource* readback,
                   const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        if (attempted)
            return S_FALSE;
        attempted = true;
        if (!actualQueue || !snapshot || !readback)
            return result = E_INVALIDARG;
        queue = actualQueue;
        source = snapshot;
        destination = readback;
        result = queue->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(result))
            return result;
        const auto type = queue->GetDesc().Type;
        if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
            return result = E_INVALIDARG;
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(result))
            return result;
        result = device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        if (FAILED(result))
            return result;
        result = device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands));
        if (FAILED(result))
            return result;
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = source.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = destination.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        // The game's snapshot ends in COPY_SOURCE. The private readback stays COPY_DEST.
        commands->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        result = commands->Close();
        if (FAILED(result))
            return result;
        ID3D12CommandList* lists[] { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        executed = true;
        result = queue->Signal(fence.Get(), 1);
        return result;
    }
    State Poll() const
    {
        if (!attempted)
            return State::NotSubmitted;
        if (device && FAILED(device->GetDeviceRemovedReason()))
            return State::DeviceRemoved;
        if (FAILED(result))
            return executed ? State::Pending : State::Failed;
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            return State::DeviceRemoved;
        return completed >= 1 ? State::Complete : State::Pending;
    }
    // A failed signal after Execute cannot prove completion: report failure but retain ownership.
    bool SignalFailed() const { return executed && FAILED(result); }
};
} // namespace DlssNr::EditDump
