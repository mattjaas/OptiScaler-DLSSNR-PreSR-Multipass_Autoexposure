#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_EditDumpReadback.h"
#include "../../OptiScaler/dlssnr/DlssNr_EditDump.h"

using Copy = DlssNr::EditDump::ReadbackCopy;
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Run(bool hardware, D3D12_COMMAND_LIST_TYPE type)
{
    Runner r(hardware);
    D3D12_COMMAND_QUEUE_DESC q {}; q.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    check(r.device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> original;
    check(r.device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator)));
    check(r.device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&original)));
    D3D12_RESOURCE_DESC d {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 3; d.Height = 2; d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto snapshot = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {}; UINT64 bytes;
    r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    auto upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto fill = [&](float base)
    {
        void* p; check(upload->Map(0, nullptr, &p));
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 12; ++x)
        {
            const float value = base + float(int(x + y * 12) - 7) * .5f;
            std::memcpy(static_cast<char*>(p) + fp.Offset + y * fp.Footprint.RowPitch + x * 4, &value, 4);
        }
        upload->Unmap(0, nullptr);
    };
    fill(0);
    D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
    src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    dst.pResource = snapshot.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    original->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {snapshot.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
    original->ResourceBarrier(1, &barrier);
    check(original->Close());
    Copy copy;
    Require(copy.Poll() == Copy::State::NotSubmitted, "Unsubmitted capture must not map");
    ID3D12CommandList* lists[] {original.Get()};
    queue->ExecuteCommandLists(1, lists);
    check(copy.Submit(queue.Get(), snapshot.Get(), readback.Get(), fp));
    const auto deadline = GetTickCount64() + 30000;
    while (copy.Poll() == Copy::State::Pending && GetTickCount64() < deadline) Sleep(1);
    Require(copy.Poll() == Copy::State::Complete, "Capture must complete without resetting/destroying the original list");
    auto verify = [&]
    {
        void* p; check(readback->Map(0, nullptr, &p));
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 12; ++x)
        {
            float got; std::memcpy(&got, static_cast<char*>(p) + fp.Offset + y * fp.Footprint.RowPitch + x * 4, 4);
            Require(got == float(int(x + y * 12) - 7) * .5f, "Capture payload changed or was uninitialized");
        }
        std::vector<char> dds;
        Require(DlssNr::EditDump::WriteRows(3, 2, static_cast<char*>(p) + fp.Offset, fp.Footprint.RowPitch,
            [&](const void* v, size_t n)
            { dds.insert(dds.end(), static_cast<const char*>(v), static_cast<const char*>(v) + n); return true; }), "DDS write failed");
        Require(dds.size() == 148 + 3 * 2 * 16, "DDS includes readback row padding");
        readback->Unmap(0, nullptr);
    };
    verify(); // The original recording still exists and has never been Reset.
    // Replay the original with changed input. Only its GPU snapshot is written again;
    // the capture-owned readback must remain the first image, including during CPU mapping.
    fill(20);
    ComPtr<ID3D12CommandAllocator> resetAllocator;
    ComPtr<ID3D12GraphicsCommandList> restoreState;
    check(r.device->CreateCommandAllocator(type, IID_PPV_ARGS(&resetAllocator)));
    check(r.device->CreateCommandList(0, type, resetAllocator.Get(), nullptr, IID_PPV_ARGS(&restoreState)));
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    restoreState->ResourceBarrier(1, &barrier); check(restoreState->Close());
    ID3D12CommandList* replay[] {restoreState.Get(), original.Get()}; queue->ExecuteCommandLists(2, replay);
    Require(copy.Submit(queue.Get(), snapshot.Get(), readback.Get(), fp) == S_FALSE, "Replay must not resubmit the capture copy");
    check(queue->Signal(r.fence.Get(), 1));
    if (r.fence->GetCompletedValue() < 1)
    {
        check(r.fence->SetEventOnCompletion(1, r.event));
        Require(WaitForSingleObject(r.event, 30000) == WAIT_OBJECT_0, "Replay fence timeout");
    }
    verify();
    Copy invalid;
    Require(FAILED(invalid.Submit(nullptr, snapshot.Get(), readback.Get(), fp)), "Missing queue accepted");
    Require(invalid.Poll() == Copy::State::Failed, "Failed setup must not become pending or complete");
}
int main(int argc, char** argv)
{
    try
    {
        const bool hardware = argc < 2 || std::string(argv[1]) != "--warp";
        Run(hardware, D3D12_COMMAND_LIST_TYPE_DIRECT);
        Run(hardware, D3D12_COMMAND_LIST_TYPE_COMPUTE);
        std::cout << "Edit dump completion PASS: DIRECT/COMPUTE, original list kept unreset, immutable capture after replay, DDS payload\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
