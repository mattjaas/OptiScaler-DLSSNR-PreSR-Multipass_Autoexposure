#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_GainPreview_Dx12.h"
#include <chrono>
#include <DirectXPackedVector.h>
#include <numeric>
#include <cmath>
#include <iomanip>
using namespace DlssNr::GainPreview;
constexpr auto Srv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto Uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void Require(bool v, const char* why)
{
    if (!v)
        throw std::runtime_error(why);
}
struct Fixture
{
    Runner r;
    unsigned w, h;
    bool half;
    std::array<ComPtr<ID3D12Resource>, 3> inputs;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    ComPtr<ID3D12Resource> output, readback, cb;
    ComPtr<ID3D12PipelineState> pso;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
    UINT64 bytes = 0;
    std::array<std::vector<float>, 3> values;
    Fixture(bool hardware, unsigned width, unsigned height, bool fp16 = false)
        : r(hardware, 4), w(width), h(height), half(fp16)
    {
        D3D12_DESCRIPTOR_RANGE ranges[2] {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 3;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 3;
        D3D12_ROOT_PARAMETER params[2] {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = { 2, ranges };
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        D3D12_ROOT_SIGNATURE_DESC desc {};
        desc.NumParameters = 2;
        desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        r.root.Reset();
        check(r.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&r.root)));
        pso = r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_gain_preview");
        cb = r.buffer(sizeof(Constants), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Width = TextureWidth;
        d.Height = TextureHeight;
        d.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        output = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        readback = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto cpu = r.heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
        u.Format = d.Format;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        r.device->CreateUnorderedAccessView(output.Get(), nullptr, &u, { cpu.ptr + 3 * SIZE_T(r.stride) });
        for (unsigned n = 0; n < 3; ++n)
        {
            const unsigned channels = n == 2 ? 2 : 4;
            values[n].resize(size_t(w) * h * channels);
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                    for (unsigned c = 0; c < channels; ++c)
                    {
                        const size_t at = (size_t(y) * w + x) * channels + c;
                        // Spatially varied signed edit with different positive counts per channel.
                        const int k = int((x * 13 + y * 31 + c * 17) % 101) - (c == 0 ? 19 : 41);
                        values[n][at] = n == 2   ? (c == 0 ? .25f + float(x % 11) * .0625f : -.125f)
                                        : n == 0 ? .5f
                                                 : .5f + float(k) * .0001220703125f;
                    }
            if (half && n != 2)
                for (auto& v : values[n])
                    v = DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(v));
            d.Width = w;
            d.Height = h;
            d.Flags = D3D12_RESOURCE_FLAG_NONE;
            d.Format = n == 2 ? DXGI_FORMAT_R32G32_FLOAT
                       : half ? DXGI_FORMAT_R16G16B16A16_FLOAT
                              : DXGI_FORMAT_R32G32B32A32_FLOAT;
            inputs[n] = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_SHADER_RESOURCE_VIEW_DESC s {};
            s.Format = d.Format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2D.MipLevels = 1;
            r.device->CreateShaderResourceView(inputs[n].Get(), &s, { cpu.ptr + n * SIZE_T(r.stride) });
        }
        r.begin();
        for (unsigned n = 0; n < 3; ++n)
            Upload(n, false);
        r.finish();
        uploads.clear();
    }
    void Upload(unsigned n, bool existing = true)
    {
        const auto d = inputs[n]->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT f {};
        UINT64 size;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &f, nullptr, nullptr, &size);
        auto upload = r.buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        char* data;
        check(upload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        const size_t count = size_t(w) * (n == 2 ? 2 : 4);
        for (unsigned y = 0; y < h; ++y)
            if (half && n != 2)
            {
                auto* row = reinterpret_cast<uint16_t*>(data + f.Footprint.RowPitch * y);
                for (size_t x = 0; x < count; ++x)
                    row[x] = DirectX::PackedVector::XMConvertFloatToHalf(values[n][y * count + x]);
            }
            else
                std::memcpy(data + f.Footprint.RowPitch * y, values[n].data() + y * count, count * 4);
        upload->Unmap(0, nullptr);
        uploads.push_back(upload);
        if (existing)
            r.barrier(inputs[n].Get(), Srv, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION a {}, b {};
        a.pResource = upload.Get();
        a.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        a.PlacedFootprint = f;
        b.pResource = inputs[n].Get();
        b.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        r.list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr);
        r.barrier(inputs[n].Get(), D3D12_RESOURCE_STATE_COPY_DEST, Srv);
    }
    void Bind(const Constants& c)
    {
        void* data;
        check(cb->Map(0, nullptr, &data));
        std::memcpy(data, &c, sizeof(c));
        cb->Unmap(0, nullptr);
        ID3D12DescriptorHeap* heaps[] { r.heap.Get() };
        r.list->SetDescriptorHeaps(1, heaps);
        r.list->SetComputeRootSignature(r.root.Get());
        r.list->SetPipelineState(pso.Get());
        r.list->SetComputeRootDescriptorTable(0, r.heap->GetGPUDescriptorHandleForHeapStart());
        r.list->SetComputeRootConstantBufferView(1, cb->GetGPUVirtualAddress());
    }
    void Probe(unsigned groups = Groups)
    {
        r.barrier(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, Uav);
        r.list->Dispatch(groups, 1, 1);
        r.barrier(output.Get(), Uav, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION a {}, b {};
        a.pResource = output.Get();
        a.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource = readback.Get();
        b.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        b.PlacedFootprint = fp;
        D3D12_BOX box { 0, 0, 0, TextureWidth, groups * 2 + 2, 1 };
        r.list->CopyTextureRegion(&b, 0, 0, 0, &a, &box);
    }
    Reading Run(const Constants& c)
    {
        r.begin();
        Bind(c);
        Probe();
        r.finish();
        void* data;
        check(readback->Map(0, nullptr, &data));
        auto s = Unpack(data, fp.Footprint.RowPitch);
        readback->Unmap(0, nullptr);
        return s;
    }
};
// Independent logarithmic bin construction, no production bit shifts or helper.
unsigned ReferenceBin(float v)
{
    if (v < std::ldexp(1.f, -24))
        return 0;
    int exponent = 0;
    const double f = std::frexp(double(v), &exponent) * 2;
    return unsigned(std::min(1023, 1 + (exponent - 1 + 24) * 32 + int(std::floor((f - 1) * 32))));
}
void Quality(bool hardware, bool half)
{
    Fixture f(hardware, 517, 293, half);
    Constants c;
    c.width = f.w;
    c.height = f.h;
    c.serialLow = 0x89abcdef;
    c.serialHigh = 0x12345678;
    auto s = f.Run(c);
    Require(s.gate == 3 && s.validMotion == MotionSamples, "Baseline gate");
    c.previous = s.motion;
    c.previousValid = 1;
    s = f.Run(c);
    Require(s.gate == 2, "Unchanged nonzero field must pause");
    c.requireChange = 0;
    for (unsigned phase : { 0u, 7u, 18u, 63u })
    {
        c.phaseX = phase % 8;
        c.phaseY = phase / 8;
        s = f.Run(c);
        Require(s.gate == 1 && s.samples == 36864 && s.validEdits == s.samples, "Usable motion");
        Require(s.serial == 0x1234567889abcdefULL, "Serial ABI");
        std::array<uint32_t, Bins> r {}, g {};
        for (unsigned y = 0; y < 144; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const unsigned sx = std::min(f.w - 1, (x * 16 + c.phaseX * 2 + 1) * f.w / (256 * 16));
                const unsigned sy = std::min(f.h - 1, (y * 16 + c.phaseY * 2 + 1) * f.h / (144 * 16));
                const size_t at = (size_t(sy) * f.w + sx) * 4;
                const float dr = f.values[1][at] - f.values[0][at], dg = f.values[1][at + 1] - f.values[0][at + 1];
                if (dr > 0)
                    ++r[ReferenceBin(dr)];
                if (dg > 0)
                    ++g[ReferenceBin(dg)];
            }
        Require(r == s.r && g == s.g, "Every sampled histogram bin must match independent reference");
        for (unsigned i = 0; i < MotionSamples; ++i)
        {
            const unsigned x = (2 * (i % 32) + 1) * f.w / 64, y = (2 * (i / 32) + 1) * f.h / 36;
            const size_t at = (size_t(y) * f.w + x) * 2;
            Require(s.motion[i * 2] == f.values[2][at] && s.motion[i * 2 + 1] == f.values[2][at + 1],
                    "Motion payload ABI");
        }
    }
    c.requireChange = 1;
    c.previous.fill(0);
    s = f.Run(c);
    Require(s.gate == 1 && s.changed == MotionSamples, "Changing motion field");
    c.changeThreshold = NAN;
    c.motionThreshold = NAN;
    s = f.Run(c);
    Require(s.gate == 1, "Shader fallback thresholds");
    std::fill(f.values[1].begin(), f.values[1].end(), NAN);
    f.r.begin();
    f.Upload(1);
    f.r.finish();
    f.uploads.clear();
    s = f.Run(c);
    Require(s.gate == 1 && s.validEdits == 0, "Invalid edits must be excluded");
    std::fill(f.values[2].begin(), f.values[2].end(), 0.f);
    f.r.begin();
    f.Upload(2);
    f.r.finish();
    f.uploads.clear();
    s = f.Run(c);
    Require(s.gate == 0 && s.moving == 0, "Zero field");
    std::fill(f.values[2].begin(), f.values[2].end(), NAN);
    f.r.begin();
    f.Upload(2);
    f.r.finish();
    f.uploads.clear();
    s = f.Run(c);
    Require(s.gate == 0 && s.validMotion == 0, "Invalid field");
    // Boundary cases on the exact sampled coordinates, not a whole-frame average.
    auto sparseMotion = [&](unsigned count)
    {
        std::fill(f.values[2].begin(), f.values[2].end(), 0.f);
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned x = (2 * (i % 32) + 1) * f.w / 64, y = (2 * (i / 32) + 1) * f.h / 36;
            f.values[2][(size_t(y) * f.w + x) * 2] = .5f;
        }
        f.r.begin();
        f.Upload(2);
        f.r.finish();
        f.uploads.clear();
    };
    sparseMotion(5);
    c.previous.fill(0);
    s = f.Run(c);
    Require(s.gate == 0 && s.moving == 5, "Five moving samples insufficient");
    sparseMotion(6);
    s = f.Run(c);
    Require(s.gate == 1 && s.moving == 6 && s.changed == 6, "Six changed moving samples sufficient");
    c.previous = s.motion;
    c.changeThreshold = .0625f;
    for (unsigned i = 0; i < 6; ++i)
        c.previous[i * 2] += .0625f;
    s = f.Run(c);
    Require(s.gate == 2 && s.changed == 0, "Change tolerance is strict");
    for (unsigned i = 0; i < 6; ++i)
        c.previous[i * 2] += .00001f;
    s = f.Run(c);
    Require(s.gate == 1 && s.changed == 6, "Above-tolerance change");
    c.width = 0;
    s = f.Run(c);
    Require(s.gate == 0 && s.samples == 0, "Invalid dimensions guard");
    std::cout << "GPU histogram, phase, signed edit and motion gate checks passed\n";
}
void Wait(Runner& r)
{
    check(r.queue->Signal(r.fence.Get(), ++r.serial));
    check(r.fence->SetEventOnCompletion(r.serial, r.event));
    Require(WaitForSingleObject(r.event, 30000) == WAIT_OBJECT_0, "Queue timeout");
}
void RuntimeQuality(bool hardware)
{
    Fixture f(hardware, 128, 72);
    Runtime preview;
    Settings settings;
    settings.requireChange = false;
    Require(!preview.Record(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, false, 0, 0), "Disabled path");
    preview.Configure(true, settings);
    std::vector<ComPtr<ID3D12GraphicsCommandList>> lists;
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
    auto submit = [&](ID3D12GraphicsCommandList* list)
    {
        check(list->Close());
        ID3D12CommandList* raw[] { list };
        f.r.queue->ExecuteCommandLists(1, raw);
        preview.SignalSubmitted(f.r.queue.Get(), 1, raw);
        preview.CopySubmitted(f.r.queue.Get(), list);
        Wait(f.r);
        preview.Collect();
    };
    for (unsigned i = 0; i < 8; ++i)
    {
        ComPtr<ID3D12CommandAllocator> a;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(f.r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)));
        check(f.r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.Get(), nullptr, IID_PPV_ARGS(&list)));
        Require(preview.Record(f.r.device.Get(), list.Get(), list.Get(), f.inputs[0].Get(), f.inputs[1].Get(),
                               f.inputs[2].Get(), f.w, f.h, false, 1, i * .25),
                "Record lease");
        Require(preview.MatchesContext(f.r.device.Get(), 1) && !preview.MatchesContext(f.r.device.Get(), 2) &&
                    !preview.MatchesContext(nullptr, 1), "Automatic gain must reject foreign preview contexts");
        submit(list.Get());
        const auto status = preview.Get();
        Require(status.valid && status.measurements == i + 1, "Readback must complete before game list Reset/release");
        // Replay is legal: immutable constants/descriptors, state cycle, one diagnostic readback.
        ID3D12CommandList* raw[] { list.Get() };
        f.r.queue->ExecuteCommandLists(1, raw);
        preview.SignalSubmitted(f.r.queue.Get(), 1, raw);
        preview.CopySubmitted(f.r.queue.Get(), list.Get());
        Wait(f.r);
        Require(preview.Get().measurements == i + 1, "Replay must not duplicate sample");
        lists.push_back(list);
        allocators.push_back(a);
    }
    f.r.begin();
    Require(!preview.Record(f.r.device.Get(), f.r.list.Get(), f.r.list.Get(), f.inputs[0].Get(), f.inputs[1].Get(),
                            f.inputs[2].Get(), f.w, f.h, false, 1, 3),
            "Finite pool must backpressure replayable recordings");
    f.r.finish();
    check(allocators[0]->Reset());
    check(lists[0]->Reset(allocators[0].Get(), nullptr));
    preview.ResetRecording(lists[0].Get());
    preview.Collect();
    Require(preview.Record(f.r.device.Get(), lists[0].Get(), lists[0].Get(), f.inputs[0].Get(), f.inputs[1].Get(),
                           f.inputs[2].Get(), f.w, f.h, false, 1, 3.25),
            "Reset must recycle completed lease");
    submit(lists[0].Get());
    Require(preview.Get().valid, "Recycled lease valid");
    // A completed but not yet consumed asynchronous result cannot unpause unsupported input.
    check(allocators[0]->Reset());
    check(lists[0]->Reset(allocators[0].Get(), nullptr));
    preview.ResetRecording(lists[0].Get());
    preview.Collect();
    Require(preview.Record(f.r.device.Get(), lists[0].Get(), lists[0].Get(), f.inputs[0].Get(), f.inputs[1].Get(),
                           f.inputs[2].Get(), f.w, f.h, false, 1, 3.5),
            "Record before pause");
    check(lists[0]->Close());
    ID3D12CommandList* raw[] { lists[0].Get() };
    f.r.queue->ExecuteCommandLists(1, raw);
    preview.SignalSubmitted(f.r.queue.Get(), 1, raw);
    preview.CopySubmitted(f.r.queue.Get(), lists[0].Get());
    preview.Pause("Test pause");
    Wait(f.r);
    Require(preview.Get().paused && preview.Get().text == "Test pause", "Stale result must not unpause");
    preview.Configure(false, settings);
    lists.clear();
    allocators.clear();
    preview.Collect();
    Require(!preview.Get().valid, "Disable resets smoother");
    std::cout << "Production runtime: async completion, replay, finite pool, recycle and pause checks passed\n";
}
void Timing(bool hardware, unsigned w, unsigned h)
{
    Fixture f(hardware, w, h, true);
    std::ofstream raw("artifacts/validation/gain-preview-timing.csv", w == 2240 ? std::ios::out : std::ios::app);
    if (w == 2240)
        raw << "width,height,comparison,quartet,window,variant,gpu_ms\n";
    raw << std::setprecision(10);
    auto single = f.r.pipeline("artifacts/validation/gain-preview", "single");
    D3D12_QUERY_HEAP_DESC desc {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2;
    ComPtr<ID3D12QueryHeap> queries;
    check(f.r.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queries)));
    auto times = f.r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency;
    check(f.r.queue->GetTimestampFrequency(&frequency));
    auto measure = [&](unsigned mode)
    {
        Constants c;
        c.width = w;
        c.height = h;
        c.requireChange = false;
        c.motionThreshold = mode == 1 ? 10.f : .1f;
        f.r.begin();
        f.Bind(c);
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (mode)
        {
            if (mode == 3)
                f.r.list->SetPipelineState(single.Get());
            f.Probe(mode == 3 ? 1 : Groups);
        }
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        f.r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, times.Get(), 0);
        f.r.finish();
        UINT64* data;
        check(times->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        double ms = double(data[1] - data[0]) * 1000 / frequency;
        times->Unmap(0, nullptr);
        return ms;
    };
    for (unsigned mode : { 1u, 2u, 4u })
    {
        const unsigned baseMode = mode == 4 ? 3 : 0, variantMode = mode == 4 ? 2 : mode;
        for (unsigned i = 0; i < 16; ++i)
        {
            measure(baseMode);
            measure(variantMode);
        }
        std::vector<double> a, b;
        for (unsigned group = 0; group < 16; ++group)
        {
            unsigned window = 0;
            for (unsigned m : { baseMode, variantMode, variantMode, baseMode })
            {
                const auto ms = measure(m);
                (m == variantMode ? b : a).push_back(ms);
                raw << w << ',' << h << ',' << mode << ',' << group << ',' << window++ << ','
                    << (m == variantMode ? 'B' : 'A') << ',' << ms << '\n';
            }
        }
        auto stats = [](const std::vector<double>& v)
        {
            const double mean = std::accumulate(v.begin(), v.end(), 0.) / v.size();
            double variance = 0;
            for (auto n : v)
                variance += (n - mean) * (n - mean);
            return std::pair<double, double> { mean, std::sqrt(variance / v.size()) };
        };
        auto base = stats(a), variant = stats(b);
        std::cout << std::fixed << std::setprecision(6) << "TIMING," << w << ',' << h << ','
                  << (mode == 1   ? "off-v-motion"
                      : mode == 2 ? "off-v-full"
                                  : "single-v-eight")
                  << ",n=" << b.size() << ",off_ms=" << base.first << ",off_sd=" << base.second
                  << ",probe_ms=" << variant.first << ",probe_sd=" << variant.second
                  << ",delta_ms=" << variant.first - base.first << '\n';
    }
}
int main(int argc, char** argv)
{
    try
    {
        const bool hardware = argc <= 1 || std::string(argv[1]) != "--warp";
        Quality(hardware, false);
        Quality(hardware, true);
        RuntimeQuality(hardware);
        if (argc > 1 && std::string(argv[1]) == "--timing")
        {
            Timing(hardware, 2240, 1264);
            Timing(hardware, 2496, 1404);
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
