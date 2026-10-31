#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_EditDumpReadback.h"
#include "../../OptiScaler/dlssnr/DlssNr_EditDump.h"

using Copy = DlssNr::EditDump::ReadbackCopy;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void Run(bool hardware, D3D12_COMMAND_LIST_TYPE type, bool batch)
{
    Runner r(hardware);
    D3D12_COMMAND_QUEUE_DESC q {};
    q.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    check(r.device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> original;
    check(r.device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator)));
    check(r.device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&original)));
    struct Plane
    {
        ComPtr<ID3D12Resource> snapshot, upload, readback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
        unsigned channels = 0;
        DlssNr::EditDump::Format format;
        std::vector<float> expected;
    };
    std::array<Plane, 3> planes;
    const auto count = batch ? 3u : 1u;
    const DXGI_FORMAT formats[] { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT };
    for (unsigned i = 0; i < count; ++i)
    {
        auto& p = planes[i];
        p.format = DlssNr::EditDump::Format(uint32_t(formats[i]));
        p.channels = i == 0 ? 4 : i == 1 ? 1 : 2;
        p.expected.resize(6 * p.channels);
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = 3;
        d.Height = 2;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = formats[i];
        p.snapshot = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        UINT64 bytes;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &p.fp, nullptr, nullptr, &bytes);
        p.upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        p.readback = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER b {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { p.snapshot.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_COPY_DEST };
        original->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = p.upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = p.fp;
        dst.pResource = p.snapshot.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        original->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        original->ResourceBarrier(1, &b);
    }
    auto fill = [&](float base, bool remember)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            auto& p = planes[i];
            void* data;
            check(p.upload->Map(0, nullptr, &data));
            for (unsigned y = 0; y < 2; ++y)
                for (unsigned x = 0; x < 3 * p.channels; ++x)
                {
                    const float v = base + float(int(x + y * 3 * p.channels) - 7) * .5f;
                    std::memcpy(static_cast<char*>(data) + p.fp.Offset + y * p.fp.Footprint.RowPitch + x * 4, &v, 4);
                    if (remember)
                        p.expected[y * 3 * p.channels + x] = v;
                }
            p.upload->Unmap(0, nullptr);
        }
    };
    fill(0, true);
    check(original->Close());
    std::array<Copy::Item, 3> items;
    for (unsigned i = 0; i < count; ++i)
        items[i] = { planes[i].snapshot.Get(), planes[i].readback.Get(), planes[i].fp };
    Copy copy;
    Require(copy.Poll() == Copy::State::NotSubmitted, "Unsubmitted capture must not map");
    ID3D12CommandList* lists[] { original.Get() };
    queue->ExecuteCommandLists(1, lists);
    if (batch)
        check(copy.SubmitMany(queue.Get(), items.data(), count));
    else
        check(copy.Submit(queue.Get(), items[0].snapshot, items[0].readback, items[0].footprint));
    const auto deadline = GetTickCount64() + 30000;
    while (copy.Poll() == Copy::State::Pending && GetTickCount64() < deadline)
        Sleep(1);
    Require(copy.Poll() == Copy::State::Complete, "Capture must complete with original list alive and unreset");
    auto verify = [&]
    {
        for (unsigned i = 0; i < count; ++i)
        {
            auto& p = planes[i];
            void* data;
            check(p.readback->Map(0, nullptr, &data));
            for (unsigned y = 0; y < 2; ++y)
                Require(std::memcmp(static_cast<char*>(data) + p.fp.Offset + y * p.fp.Footprint.RowPitch,
                                    p.expected.data() + y * 3 * p.channels, 3 * p.channels * 4) == 0,
                        "Capture changed/uninitialized");
            std::vector<char> dds;
            Require(DlssNr::EditDump::WriteRows(
                        3, 2, static_cast<char*>(data) + p.fp.Offset, p.fp.Footprint.RowPitch,
                        [&](const void* v, size_t n)
                        {
                            dds.insert(dds.end(), static_cast<const char*>(v), static_cast<const char*>(v) + n);
                            return true;
                        },
                        p.format),
                    "DDS write failed");
            Require(dds.size() == 148 + 6 * p.channels * 4, "DDS row padding leaked");
            uint32_t format;
            std::memcpy(&format, dds.data() + 128, 4);
            Require(format == uint32_t(formats[i]), "DDS guide format changed");
            p.readback->Unmap(0, nullptr);
        }
    };
    verify();
    fill(20, false);
    // Replay without a separate repair recording: snapshot state cycles inside
    // the original list, exactly as the production capture now does.
    queue->ExecuteCommandLists(1, lists);
    Require(copy.SubmitMany(queue.Get(), items.data(), count) == S_FALSE, "Replay must not resubmit capture copy");
    check(queue->Signal(r.fence.Get(), 1));
    if (r.fence->GetCompletedValue() < 1)
    {
        check(r.fence->SetEventOnCompletion(1, r.event));
        Require(WaitForSingleObject(r.event, 30000) == WAIT_OBJECT_0, "Replay fence timeout");
    }
    verify();
    Copy invalid;
    Require(FAILED(invalid.Submit(nullptr, items[0].snapshot, items[0].readback, items[0].footprint)),
            "Missing queue accepted");
    Require(invalid.Poll() == Copy::State::Failed, "Failed setup must not become pending or complete");
    Copy invalidCount;
    Require(FAILED(invalidCount.SubmitMany(queue.Get(), items.data(), 4)), "Oversized batch accepted");
    Copy missing;
    Require(FAILED(missing.SubmitMany(queue.Get(), nullptr, 1)), "Missing batch accepted");
}
int main(int argc, char** argv)
{
    try
    {
        const bool hardware = argc < 2 || std::string(argv[1]) != "--warp";
        for (bool batch : { false, true })
        {
            Run(hardware, D3D12_COMMAND_LIST_TYPE_DIRECT, batch);
            Run(hardware, D3D12_COMMAND_LIST_TYPE_COMPUTE, batch);
        }
        std::cout << "Edit dump completion PASS: single/batch, DIRECT/COMPUTE, unreset lists, replay state cycle, "
                     "immutable readbacks, DDS payloads\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
