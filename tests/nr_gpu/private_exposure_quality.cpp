#include "pch.h"
#include "../../OptiScaler/shaders/dlssnr/superior/Fields.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"
#include "interpass_fixture.h"
#include "packed_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_PrivateExposure.h"
#include <cmath>
#include <DirectXPackedVector.h>
#include <limits>
#include <map>
#include "../../OptiScaler/dlssnr/DlssNr_EditDump.h"

namespace PE = DlssNr::PrivateExposure;
constexpr auto SRV = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

// Independent scalar reference: double-precision arithmetic, natural log/exp and an
// exhaustive search of positive binary16 values, rather than the GPU reduction/function.
float Half(uint32_t bits)
{
    const uint32_t exponent = (bits >> 10) & 31u, mantissa = bits & 1023u;
    if (exponent == 31u) return mantissa ? NAN : INFINITY;
    return exponent == 0u ? std::ldexp(float(mantissa), -24)
                          : std::ldexp(1.f + float(mantissa) / 1024.f, int(exponent) - 15);
}
float ReferenceHalf(double v)
{
    if (!(v > 0) || !std::isfinite(v)) return 1.f;
    uint32_t lo = 0, hi = 0x7bff;
    while (lo < hi)
    {
        const auto mid = (lo + hi + 1) / 2;
        if (Half(mid) <= v) lo = mid; else hi = mid - 1;
    }
    return lo ? Half(lo) : 1.f;
}
void Near(float got, float expected, const char* label)
{
    // Up to one binary16 interval from FP32 log/reduction approximation at a boundary.
    if (!std::isfinite(got) || std::abs(got - expected) > std::max(2e-6f, std::abs(expected) * .0011f))
        throw std::runtime_error(std::string(label) + ": got " + std::to_string(got) + " expected " + std::to_string(expected));
}
void InverseNear(float got, float expected, float gain, const char* label)
{
    // The reciprocal transport multiplies storage/arithmetic error as well as the signal.
    // Compare in the transported units; exposure tests keep their original independent bounds.
    const float scale = std::min(gain, 1.f);
    Near(got * scale, expected * scale, label);
}
struct Fixture
{
    Runner r;
    ComPtr<ID3D12PipelineState> meter, carrier;
    ComPtr<ID3D12QueryHeap> timingQueries;
    ComPtr<ID3D12Resource> timingReadback;
    UINT64 timingFrequency = 0;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    // Immutable PSOs belong to this device/root signature. Histories/resources remain per sequence.
    std::map<std::string, ComPtr<ID3D12PipelineState>> visibilityPipelines;
    ComPtr<ID3D12PipelineState> visibilityPipeline(const std::string& name)
    {
        auto& p = visibilityPipelines[name];
        if (!p)
            p = r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", name);
        return p;
    }

    Fixture(bool hardware) : r(hardware, 64, 2, true)
    {
        const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
        meter = r.pipeline(dir, "private_exposure");
        carrier = r.pipeline(dir, "private_carrier");
    }
    ComPtr<ID3D12Resource> texture(UINT w, UINT h, const std::vector<float>* values = nullptr,
                                 DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w; d.Height = h;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = format;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto t = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, values ? D3D12_RESOURCE_STATE_COPY_DEST : UAV);
        if (values)
        {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {}; UINT64 bytes;
            r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
            auto b = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            char* p; check(b->Map(0, nullptr, reinterpret_cast<void**>(&p)));
            const UINT channels =
                (format == DXGI_FORMAT_R32G32_FLOAT || format == DXGI_FORMAT_R32G32_UINT) ? 2u
                : (format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R16_FLOAT)
                    ? 1u
                    : 4u;
            for (UINT y = 0; y < h; ++y)
                for (UINT x = 0; x < w; ++x)
                    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R16_FLOAT)
                    {
                        auto* half = reinterpret_cast<uint16_t*>(p + fp.Footprint.RowPitch * y + x * channels * 2);
                        for (unsigned ch = 0; ch < channels; ++ch)
                            half[ch] =
                                DirectX::PackedVector::XMConvertFloatToHalf((*values)[(size_t(y) * w + x) * 4 + ch]);
                    }
                    else
                        std::memcpy(p + fp.Footprint.RowPitch * y + x * channels * 4,
                                    values->data() + (size_t(y) * w + x) * 4, channels * 4);
            b->Unmap(0, nullptr);
            D3D12_TEXTURE_COPY_LOCATION a {}, z {};
            a.pResource = b.Get(); a.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; a.PlacedFootprint = fp;
            z.pResource = t.Get(); z.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            r.list->CopyTextureRegion(&z, 0, 0, 0, &a, nullptr);
            r.barrier(t.Get(), D3D12_RESOURCE_STATE_COPY_DEST, SRV);
            uploads.push_back(b);
        }
        return t;
    }
    void dispatch(ID3D12PipelineState* ps, const DlssNr::Superior::DlssNrConstants& c, ID3D12Resource* source,
                  ID3D12Resource* model, ID3D12Resource* target, UINT gx, UINT gy, ID3D12Resource* depth = nullptr,
                  ID3D12Resource* rawProxy = nullptr, ID3D12Resource* rawAnswer = nullptr,
                  ID3D12Resource* guideMotion = nullptr, double* gpuMs = nullptr, UINT tableSlot = 0,
                  bool submit = true, ID3D12Resource* keep = nullptr, ID3D12Resource* transport = nullptr)
    {
        auto cpu = r.heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(tableSlot) * 8 * r.stride;
        for (UINT i = 0; i < 6; ++i)
        {
            auto* input = i == 5 && guideMotion ? guideMotion
                          : i == 3 && depth     ? depth
                          : i == 2 && rawProxy  ? rawProxy
                          : i == 4 && rawAnswer ? rawAnswer
                          : i == 1              ? model
                                                : source;
            D3D12_SHADER_RESOURCE_VIEW_DESC d {};
            d.Format = input->GetDesc().Format;
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels = 1;
            r.device->CreateShaderResourceView(input, &d, { cpu.ptr + SIZE_T(i) * r.stride });
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
        u.Format = target->GetDesc().Format; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        for (UINT i = 6; i < 8; ++i)
        {
            auto* write = i == 7 && keep ? keep : target;
            u.Format = write->GetDesc().Format;
            if (write->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                u.Format = DXGI_FORMAT_R32_TYPELESS;
                u.Buffer.NumElements = 64;
                u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            r.device->CreateUnorderedAccessView(write, nullptr, &u, { cpu.ptr + SIZE_T(i) * r.stride });
        }
        auto b = r.buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p; check(b->Map(0, nullptr, &p)); std::memcpy(p, &c, 256); b->Unmap(0, nullptr);
        r.list->SetComputeRootSignature(r.root.Get());
        if (transport)
            r.list->SetComputeRootConstantBufferView(2, transport->GetGPUVirtualAddress());
        ID3D12DescriptorHeap* heaps[] { r.heap.Get() }; r.list->SetDescriptorHeaps(1, heaps);
        auto gpu = r.heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += UINT64(tableSlot) * 8 * r.stride;
        r.list->SetComputeRootDescriptorTable(0, gpu);
        r.list->SetComputeRootConstantBufferView(1, b->GetGPUVirtualAddress());
        r.list->SetPipelineState(ps);
        if (!submit)
        {
            if (gpuMs)
                throw std::runtime_error("Batch timestamps belong to the caller");
            uploads.push_back(b);
            r.list->Dispatch(gx, gy, 1);
            return;
        }
        if (gpuMs && !timingQueries)
        {
            D3D12_QUERY_HEAP_DESC q {};
            q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            q.Count = 2;
            check(r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&timingQueries)));
            timingReadback = r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            check(r.queue->GetTimestampFrequency(&timingFrequency));
        }
        if (gpuMs)
            r.list->EndQuery(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        r.list->Dispatch(gx, gy, 1);
        if (gpuMs)
        {
            r.list->EndQuery(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            r.list->ResolveQueryData(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timingReadback.Get(), 0);
        }
        // Finish before rewriting this fixture's descriptor table (the host uses a ring).
        r.finish(); uploads.clear(); r.begin();
        if (gpuMs)
        {
            UINT64* times;
            check(timingReadback->Map(0, nullptr, reinterpret_cast<void**>(&times)));
            *gpuMs = double(times[1] - times[0]) * 1000 / timingFrequency;
            timingReadback->Unmap(0, nullptr);
        }
    }
    std::vector<float> read(ID3D12Resource* t)
    {
        auto d = t->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {}; UINT64 bytes;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto b = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        r.barrier(t, UAV, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION a {}, z {};
        a.pResource = t; a.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        z.pResource = b.Get(); z.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; z.PlacedFootprint = fp;
        r.list->CopyTextureRegion(&z, 0, 0, 0, &a, nullptr); r.finish(); uploads.clear();
        char* p; check(b->Map(0, nullptr, reinterpret_cast<void**>(&p)));
        std::vector<float> out(size_t(d.Width) * d.Height * 4);
        const UINT channels = (d.Format == DXGI_FORMAT_R32G32_FLOAT || d.Format == DXGI_FORMAT_R32G32_UINT) ? 2u
                              : (d.Format == DXGI_FORMAT_R32_FLOAT || d.Format == DXGI_FORMAT_R32_UINT ||
                                 d.Format == DXGI_FORMAT_R16_FLOAT)
                                  ? 1u
                                  : 4u;
        const bool half = d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format == DXGI_FORMAT_R16_FLOAT;
        for (UINT y = 0; y < d.Height; ++y)
            for (UINT x = 0; x < d.Width; ++x)
                if (half)
                {
                    const auto* code =
                        reinterpret_cast<const uint16_t*>(p + fp.Footprint.RowPitch * y + x * channels * 2);
                    for (unsigned c = 0; c < channels; ++c)
                        out[(size_t(y) * d.Width + x) * 4 + c] = (code[c] & 0x8000u ? -1.f : 1.f) * Half(code[c] & 0x7fffu);
                }
                else
                    std::memcpy(out.data() + (size_t(y) * d.Width + x) * 4,
                                p + fp.Footprint.RowPitch * y + x * channels * 4, channels * 4);
        b->Unmap(0, nullptr); return out;
    }
    std::vector<float> exposure(const std::vector<float>& image, UINT w, UINT h, UINT sw, UINT sh,
                                UINT method, float pre, bool measure = true, bool reset = true, float old = 1.f,
                                UINT ox = 0, UINT oy = 0)
    {
        r.begin(); const UINT aw = w + ox, ah = h + oy;
        auto input = texture(aw, ah, &image);
        std::vector<float> state { old, old, 0, 0 };
        auto previous = texture(1, 1, &state, DXGI_FORMAT_R32G32_FLOAT),
             sums = texture((sw + 31) / 32, (sh + 31) / 32, nullptr, DXGI_FORMAT_R32_FLOAT),
             output = texture(1, 1, nullptr, DXGI_FORMAT_R32G32_FLOAT);
        DlssNr::Superior::DlssNrConstants c {};
        c.Mode = 40; c.Width = sw; c.Height = sh; c.ExposureMode = method; c.PreExposure = pre;
        c.ExposureSourceWidth = w; c.ExposureSourceHeight = h;
        c.ResidualMotionBaseX = ox; c.ResidualMotionBaseY = oy;
        if (measure)
        {
            dispatch(meter.Get(), c, input.Get(), previous.Get(), sums.Get(), (sw + 31) / 32, (sh + 31) / 32);
            r.barrier(sums.Get(), UAV, SRV);
        }
        c.Mode = 41; c.Width = (sw + 31) / 32; c.Height = (sh + 31) / 32;
        c.DebugView = measure; c.ApplyModel = reset; c.ResidualHistoryValid = 1;
        dispatch(meter.Get(), c, measure ? sums.Get() : input.Get(), previous.Get(), output.Get(), 1, 1);
        return read(output.Get());
    }
};

void Timing(Fixture& f, const std::string& saturationBaseline = "", float saturation = 1.f, float depthStrength = 0.f,
            bool fullAnswer = false, float gain = 16.f, unsigned depthFlags = 0, bool measureExtract = false, bool measureBlend = false)
{
    // Synthetic changed stages only: no NR model, NGX evaluate, game scene or final composition.
    // Resident FP16 carrier and GPU timestamps, warmup and ABBA. No allocation/upload/fence in a window.
    auto& r = f.r;
    auto baseline = saturationBaseline.empty()
        ? r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "dlssnr_superior_carrier")
        : r.pipeline(saturationBaseline, "private_carrier");
    const std::string csvPath = measureBlend ? "artifacts/validation/private-fade-timing.csv" : measureExtract ? "artifacts/validation/no-dlss-extract-timing.csv" : saturationBaseline.empty() ? "artifacts/private-exposure-timing.csv"
        : "artifacts/private-" + std::string(fullAnswer ? "answer-" : "transport-") + "g" + std::to_string(int(gain)) +
            "-s" + std::to_string(int(saturation)) + "-d" + std::to_string(int(depthStrength)) + "-flags" + std::to_string(depthFlags) + "-timing.csv";
    std::ofstream csv(csvPath);
    csv << "width,height,method,block,position,variant,ms\n";
    const std::pair<UINT, UINT> first = measureExtract ? std::pair<UINT, UINT>{2496, 1404} : std::pair<UINT, UINT>{1920, 1080};
    for (auto size : { first, {3840, 2160} })
    {
        const UINT w = size.first, h = size.second, sw = w / 2, sh = h / 2, bx = (sw + 31) / 32, by = (sh + 31) / 32;
        std::vector<float> a(size_t(w) * h * 4, .5f), b(a.size());
        for (size_t i = 0; i < b.size(); ++i) b[i] = .5f + .02f * std::sin(float(i % 997) * .013f);
        r.begin(); auto in = f.texture(w, h, &a), model = f.texture(w, h, &b),
             depth = f.texture(w, h, &b, DXGI_FORMAT_R32_FLOAT);
        auto proxy = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
             answer = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
        DlssNr::Superior::DlssNrConstants seed {}; seed.Mode = 37; seed.Width = w; seed.Height = h; seed.WhitePoint = 1;
        f.dispatch(f.carrier.Get(), seed, in.Get(), in.Get(), proxy.Get(), (w + 7) / 8, (h + 7) / 8);
        f.dispatch(f.carrier.Get(), seed, in.Get(), model.Get(), answer.Get(), (w + 7) / 8, (h + 7) / 8);
        r.barrier(proxy.Get(), UAV, SRV); r.barrier(answer.Get(), UAV, SRV);
        auto encoded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
             decoded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
             sums = f.texture(bx, by, nullptr, DXGI_FORMAT_R32_FLOAT),
             output = f.texture(1, 1, nullptr, DXGI_FORMAT_R32G32_FLOAT);
        std::vector<float> state { 1, 1, 0, 0 }; auto previous = f.texture(1, 1, &state, DXGI_FORMAT_R32G32_FLOAT);
        r.finish(); f.uploads.clear();
        auto table = [&](UINT offset, ID3D12Resource* source, ID3D12Resource* modelTex, ID3D12Resource* target)
        {
            auto cpu = r.heap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < 6; ++i)
            {
                auto* input = i == 3 ? depth.Get() : i == 2 ? proxy.Get() : i == 4 ? answer.Get() : i == 1 ? modelTex : source;
                D3D12_SHADER_RESOURCE_VIEW_DESC d {}; d.Format = input->GetDesc().Format;
                d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                d.Texture2D.MipLevels = 1;
                r.device->CreateShaderResourceView(input, &d, { cpu.ptr + SIZE_T(offset + i) * r.stride });
            }
            D3D12_UNORDERED_ACCESS_VIEW_DESC d {}; d.Format = target->GetDesc().Format;
            d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            for (UINT i = 6; i < 8; ++i) r.device->CreateUnorderedAccessView(target, nullptr, &d, { cpu.ptr + SIZE_T(offset + i) * r.stride });
        };
        table(0, proxy.Get(), answer.Get(), encoded.Get()); table(8, encoded.Get(), previous.Get(), sums.Get());
        table(16, sums.Get(), previous.Get(), output.Get()); table(24, encoded.Get(), fullAnswer ? proxy.Get() : previous.Get(), decoded.Get());
        auto constants = [&](const DlssNr::Superior::DlssNrConstants& c)
        {
            auto cb = r.buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            void* p; check(cb->Map(0, nullptr, &p)); std::memcpy(p, &c, 256); cb->Unmap(0, nullptr); return cb;
        };
        for (UINT method : { PE::Native, PE::LegacyCNN, PE::Transformer2 })
        {
            if ((measureExtract || measureBlend) && method != PE::Native)
                continue;
            DlssNr::Superior::DlssNrConstants extraction = seed;
            extraction.Mode = 43;
            auto extractCb = constants(extraction);
            DlssNr::Superior::DlssNrConstants c = seed; c.WhitePoint = gain; c.MaxRatio = saturation; c.DirectDetailMode = fullAnswer ? 1u : 0u;
            c.ResidualScale = depthStrength; c.ResidualBlend = 1.f;
            c.DirectResolveUpscaler = depthFlags;
            c.ExposureAnchors[0] = .125f; c.ExposureAnchors[1] = .75f;
            c.ExposureAnchors[2] = .25f; c.ExposureAnchors[3] = .25f;
            auto encodeCb = constants(c); c.Mode = 38; auto decodeCb = constants(c);
            const float blendWeight=.5f;
            std::memcpy(&c.ExposurePadding,&blendWeight,4);
            auto blendCb=constants(c);
            c = {}; c.Mode = 40; c.Width = sw; c.Height = sh; c.ExposureMode = method; c.PreExposure = 1;
            c.ExposureSourceWidth = w; c.ExposureSourceHeight = h;
            auto meterCb = constants(c); c.Mode = 41; c.Width = bx; c.Height = by; c.DebugView = 1; c.ApplyModel = 1;
            auto reduceCb = constants(c);
            D3D12_QUERY_HEAP_DESC q {}; q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; q.Count = 128;
            ComPtr<ID3D12QueryHeap> queries; check(r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
            auto times = r.buffer(q.Count * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            UINT64 frequency; check(r.queue->GetTimestampFrequency(&frequency));
            auto bind = [&](ID3D12PipelineState* ps, ID3D12Resource* cb, UINT slot, UINT gx, UINT gy)
            {
                auto gpu = r.heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += UINT64(slot) * r.stride;
                r.list->SetPipelineState(ps); r.list->SetComputeRootDescriptorTable(0, gpu);
                r.list->SetComputeRootConstantBufferView(1, cb->GetGPUVirtualAddress()); r.list->Dispatch(gx, gy, 1);
            };
            auto draw = [&](bool candidate)
            {
                if (measureExtract)
                {
                    if (candidate)
                    {
                        bind(f.carrier.Get(), extractCb.Get(), 0, (w + 7) / 8, (h + 7) / 8);
                        r.barrier(encoded.Get(), UAV, SRV);
                        r.barrier(encoded.Get(), SRV, UAV);
                    }
                    return;
                }
                bind(candidate || measureBlend ? f.carrier.Get() : baseline.Get(), encodeCb.Get(), 0, (w + 7) / 8, (h + 7) / 8);
                r.barrier(encoded.Get(), UAV, SRV);
                if ((candidate || !saturationBaseline.empty()) && method != PE::Native)
                {
                    bind(f.meter.Get(), meterCb.Get(), 8, bx, by); r.barrier(sums.Get(), UAV, SRV);
                    bind(f.meter.Get(), reduceCb.Get(), 16, 1, 1); r.barrier(sums.Get(), SRV, UAV);
                    r.barrier(output.Get(), UAV, SRV); r.barrier(output.Get(), SRV, UAV);
                }
                bind(candidate || measureBlend ? f.carrier.Get() : baseline.Get(),
                     measureBlend && candidate ? blendCb.Get() : decodeCb.Get(), 24, (w + 7) / 8, (h + 7) / 8);
                r.barrier(encoded.Get(), SRV, UAV);
                D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = decoded.Get(); r.list->ResourceBarrier(1, &barrier);
            };
            auto roots = [&]
            {
                r.list->SetComputeRootSignature(r.root.Get());
                ID3D12DescriptorHeap* heaps[] { r.heap.Get() };
                r.list->SetDescriptorHeaps(1, heaps);
            };
            // Separate warmup for each variant; no warmup samples enter the report.
            for (bool candidate : { false, true })
            {
                r.begin(); roots(); for (UINT i = 0; i < 64; ++i) draw(candidate); r.finish();
            }
            for (UINT block = 0; block < 4; ++block)
            {
                r.begin(); roots();
                for (UINT i = 0; i < 64; ++i)
                {
                    r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i * 2);
                    draw(i % 4 == 1 || i % 4 == 2);
                    r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i * 2 + 1);
                }
                r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 128, times.Get(), 0); r.finish();
                UINT64* data; check(times->Map(0, nullptr, reinterpret_cast<void**>(&data)));
                for (UINT i = 0; i < 64; ++i)
                    csv << w << ',' << h << ',' << method << ',' << block << ',' << i << ','
                        << (i % 4 == 1 || i % 4 == 2 ? 'B' : 'A') << ','
                        << double(data[i * 2 + 1] - data[i * 2]) * 1000. / frequency << '\n';
                times->Unmap(0, nullptr);
            }
        }
    }
    std::cout << "Synthetic ABBA GPU stage timings written to " << csvPath << '\n';
}

#include "private_ghost_quality.inl"
#include "guided_ghost_quality.inl"
#include "temporal_ghost_quality.inl"
#include "compact_ghost_quality.inl"
#include "compact_fast_quality.inl"
#include "coarse_ghost_quality.inl"
#include "static_visibility_quality.inl"
#include "ghost_contrast_quality.inl"
#include "road_visibility_quality.inl"
#include "sky_feather_quality.inl"
#include "coarse_boundary_rings_quality.inl"
#include "edit_transport_quality.inl"

int main(int argc, char** argv)
{
    try
    {
        std::cout.setf(std::ios::unitbuf);
        Fixture f(argc < 2 || (std::string(argv[1]) != "--warp" && std::string(argv[1]) != "--contrast-quality-warp" &&
                               std::string(argv[1]) != "--sky-feather-quality-warp" &&
                               std::string(argv[1]) != "--transport-quality-warp"));
        if (argc == 3 && std::string(argv[1]) == "--transport-timing")
        {
            EditTransportTiming(f, argv[2]);
            return 0;
        }
        if (argc == 2 &&
            (std::string(argv[1]) == "--transport-quality" || std::string(argv[1]) == "--transport-quality-warp"))
        {
            EditTransportQuality(f);
            AffineGhostQuality(f);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--brightness-contrast-timing")
        {
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true, true, true, true);
            return 0;
        }
        if (argc == 2 &&
            (std::string(argv[1]) == "--sky-feather-quality" || std::string(argv[1]) == "--sky-feather-quality-warp"))
        {
            SkyVisibilityQuality(f);
            OutwardFeatherQuality(f);
            CoarseBoundaryRingsQuality(f);
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--rings-timing")
        {
            unsigned layers = unsigned(std::stoul(argv[4]));
            if (layers < 1 || layers > 3)
                throw std::runtime_error("Expected 1-3 coarse layers");
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true, true, false, false, false, true,
                                   false, layers);
            return 0;
        }
        if (argc == 4 && (std::string(argv[1]) == "--sky-timing" || std::string(argv[1]) == "--feather-timing" ||
                          std::string(argv[1]) == "--feather-fast-timing"))
        {
            bool feather = std::string(argv[1]) != "--sky-timing";
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true, feather, false, false, !feather,
                                   feather, std::string(argv[1]) == "--feather-fast-timing");
            return 0;
        }
        if (argc == 4 &&
            (std::string(argv[1]) == "--sky-baseline-preview" || std::string(argv[1]) == "--feather-preview"))
        {
            bool old = std::string(argv[1]) == "--sky-baseline-preview";
            FrozenVisibilityPreview(f, argv[2], unsigned(std::stoul(argv[3])), false, old, !old);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--road-visibility-timing")
        {
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true, false, true);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--road-quality")
        {
            RoadVisibilityQuality(f);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--road-baseline")
        {
            RoadVisibilityQuality(f, true);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--frozen-visibility-baseline")
        {
            FrozenVisibilityPreview(f, argv[2], unsigned(std::stoul(argv[3])), true);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--frozen-visibility-preview")
        {
            FrozenVisibilityPreview(f, argv[2], unsigned(std::stoul(argv[3])));
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--contrast-timing")
        {
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true, true);
            return 0;
        }
        if (argc == 2 &&
            (std::string(argv[1]) == "--contrast-quality" || std::string(argv[1]) == "--contrast-quality-warp"))
        {
            GhostContrastQuality(f);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--interior-visibility-timing")
        {
            StaticVisibilityTiming(f, argv[2], std::string(argv[3]) == "moving", true);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--wide-visibility-quality")
        {
            WideVisibilityQuality(f);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--wide-visibility-baseline")
        {
            WideVisibilityQuality(f, true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--moving-visibility-timing")
        {
            StaticVisibilityTiming(f, argv[2], true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--static-visibility-timing")
        {
            StaticVisibilityTiming(f, argv[2]);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--static-visibility-quality")
        {
            StaticVisibilityQuality(f);
            RelativeVisibilityQuality(f);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--static-visibility-capture")
        {
            StaticVisibilityCapture(f, argv[2]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--coarse-ghost-timing")
        {
            const unsigned radius = unsigned(std::stoul(argv[3]));
            if (radius < 1 || radius > 2)
                throw std::runtime_error("Coarse radius must be 1 or 2");
            CoarseGhostTiming(f, argv[2], radius);
            CoarseGhostTiming(f, argv[2], radius, true);
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--compact-math-timing")
        {
            const float gain = std::stof(argv[3]), fade = std::stof(argv[4]);
            if (!std::isfinite(gain) || gain < .01f || gain > 1024 || !std::isfinite(fade) || fade < 0 || fade > 1)
                throw std::runtime_error("Invalid math timing gain/fade");
            CompactGhostTiming(f, argv[2], true, false, true, true, true, gain, fade);
            CompactGhostTiming(f, argv[2], true, true, true, true, true, gain, fade);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--compact-fast-timing")
        {
            CompactGhostTiming(f, argv[2], true, false, true, true);
            CompactGhostTiming(f, argv[2], true, true, true, true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--compact-retirement-timing")
        {
            CompactGhostTiming(f, argv[2], true, false, true);
            CompactGhostTiming(f, argv[2], true, true, true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--compact-revision-timing")
        {
            CompactGhostTiming(f, argv[2], true);
            CompactGhostTiming(f, argv[2], true, true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--compact-ghost-timing")
        {
            CompactGhostTiming(f, argv[2]);
            CompactGhostTiming(f, argv[2], false, true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--temporal-ghost-timing")
        {
            TemporalGhostTiming(f, argv[2]);
            return 0;
        }
        if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--guided-ghost-capture")
        {
            GuidedGhostCapture(f, argv[2], argc == 4);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--ghost-mask-timing")
        {
            GhostTiming(f,true);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--ghost-timing")
        {
            GhostTiming(f);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--fade-timing")
        {
            Timing(f, "", 1.f, 0.f, false, 1.f, 0, false, true);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--extract-timing")
        {
            Timing(f, "", 1.f, 0.f, false, 1.f, 0, true);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--timing") { Timing(f); return 0; }
        if (argc == 4 && std::string(argv[1]) == "--saturation-timing")
        {
            Timing(f, argv[2], std::stof(argv[3])); return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--transport-timing")
        {
            Timing(f, argv[2], std::stof(argv[3]), std::stof(argv[4])); return 0;
        }
        if (argc == 6 && std::string(argv[1]) == "--nr-answer-timing")
        {
            Timing(f, argv[2], std::stof(argv[3]), std::stof(argv[4]), true, std::stof(argv[5])); return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--depth-timing")
        {
            Timing(f, argv[2], 1.f, 0.f, false, 16.f, unsigned(std::stoul(argv[3]))); return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--transport-parity")
        {
            const auto old = f.r.pipeline(argv[2], "private_carrier");
            unsigned cases = 0;
            for (UINT mode : {37u, 38u}) for (float gain : {1.f, 16.f, 1024.f})
                for (bool half : {false, true}) for (int fixture = 0; fixture < 4; ++fixture)
                {
                    DlssNr::Superior::DlssNrConstants c {}; c.Mode = mode; c.Width = 65; c.Height = 37; c.WhitePoint = gain; c.MaxRatio = 1;
                    if (f.r.run(old.Get(), 65, 37, 65, 37, c, fixture, half) !=
                        f.r.run(f.carrier.Get(), 65, 37, 65, 37, c, fixture, half))
                        throw std::runtime_error("Disabled colour transport changed previous shader output");
                    ++cases;
                }
            std::cout << "Disabled colour transport bit parity PASS: " << cases << " cases\n"; return 0;
        }
        EditTransportQuality(f);
        AffineGhostQuality(f);
        GhostQuality(f);
        GuidedGhostQuality(f);
        TemporalGhostQuality(f);
        StaticVisibilityQuality(f);
        RelativeVisibilityQuality(f);
        WideVisibilityQuality(f);
        RoadVisibilityQuality(f);
        GhostContrastQuality(f);
        SkyVisibilityQuality(f);
        OutwardFeatherQuality(f);
        CoarseBoundaryRingsQuality(f);
        CompactGhostQuality(f);
        CompactTransportQuality(f);
        CompactGhostQuality(f, true);
        CompactTransportQuality(f, true);
        CompactFastStressQuality(f);
        CompactWaveScatterQuality(f);
        CoarseGhostQuality(f);
        GhostMaskQuality(f);
        unsigned checks = 0;
        for (bool full : { false, true })
        for (float rawWeight : { 0.f, .25f, .5f, 1.f })
        {
            std::vector<float> proxy { .5f, .25f, .75f, 1.f };
            std::vector<float> answer { .75f, .125f, 1.f, 1.f };
            std::vector<float> filtered { .6f, .4f, .5f, 1.f };
            if (full)
                for (unsigned ch=0;ch<3;++ch)filtered[ch]+=proxy[ch]-.5f;
            f.r.begin();
            auto p=f.texture(1,1,&proxy),a=f.texture(1,1,&answer),input=f.texture(1,1,&filtered),out=f.texture(1,1);
            DlssNr::Superior::DlssNrConstants c {};
            c.Mode=38;c.Width=c.Height=1;c.WhitePoint=2;c.MaxRatio=1;c.DirectDetailMode=full;
            std::memcpy(&c.ExposurePadding,&rawWeight,4);
            f.dispatch(f.carrier.Get(),c,input.Get(),p.Get(),out.Get(),1,1,nullptr,p.Get(),a.Get());
            const auto actual=f.read(out.Get());
            for(unsigned ch=0;ch<3;++ch)
            {
                const double v=filtered[ch]-(full?proxy[ch]:.5);
                const double decoded=v>0?v/2:v;
                Near(actual[ch],float((1-rawWeight)*decoded+rawWeight*(answer[ch]-proxy[ch])),
                     "Private DLSS fade independent reference");
            }
            ++checks;
        }

        // No DLSS uses mode 43 at unit gain and writes the signed edit to FP16.
        {
            const std::vector<float> proxy { .5f, .5f, .5f, 1.f, .25f, .5f, .5f, 1.f };
            const std::vector<float> answer { .125f, .5f, .75f, 1.f, 1.5f, NAN, INFINITY, 1.f };
            f.r.begin();
            auto source = f.texture(2, 1, &proxy), model = f.texture(2, 1, &answer);
            auto edit = f.texture(2, 1, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
            DlssNr::Superior::DlssNrConstants c {};
            c.Mode = 43;
            c.Width = 2;
            c.Height = 1;
            c.WhitePoint = 1.f;
            f.dispatch(f.carrier.Get(), c, source.Get(), model.Get(), edit.Get(), 1, 1);
            const auto actual = f.read(edit.Get());
            const std::vector<float> expected { -.375f, 0.f, .25f, 1.f, 1.25f, 0.f, 0.f, 1.f };
            if (actual != expected)
                throw std::runtime_error("Direct signed NR edit differs from expected FP16 output");
            ++checks;
        }

        for (UINT method : { PE::LegacyCNN, PE::LegacyK, PE::Transformer2 })
            for (float value : { 0.f, .00001f, .05f, .1f, .18f, .5f, 1.f, 2.f, 20000.f })
                for (float pre : { 1.f, 4.f })
                {
                    const UINT w = 65, h = 37, sw = 33, sh = 19;
                    std::vector<float> image(w * h * 4, value);
                    const auto result = f.exposure(image, w, h, sw, sh, method, pre);
                    const double y = value / double(pre);
                    const float expected = ReferenceHalf(method == PE::Transformer2
                        ? .23547087609767914 / std::max(std::min(double(value), 10000.), .005)
                        : (.18 / .82) * std::exp(y > 0 ? -std::log(y + 1e-6) : 0));
                    Near(result[0], expected, "uniform exposure");
                    Near(result[1], method == PE::Transformer2 ? 0.f : result[0], "reset previous");
                    ++checks;
                }
        // Nonuniform picture, invalid values, independent input origin, up/downsampled grids.
        for (UINT method : { PE::LegacyCNN, PE::LegacyK, PE::Transformer2 })
            for (auto grid : { std::pair<UINT, UINT>{17, 9}, {79, 43}, {256, 256} })
            {
                constexpr UINT w = 65, h = 37, ox = 3, oy = 5, aw = w + ox, ah = h + oy;
                std::vector<float> image(aw * ah * 4, 12345.f);
                for (UINT y = 0; y < h; ++y)
                    for (UINT x = 0; x < w; ++x)
                    {
                        float value = (x + y * w) % 10 == 0 ? 500.f : .01f;
                        if (x == 0) value = -1.f;
                        if (x == 1) value = NAN;
                        if (x == 2) value = INFINITY;
                        for (UINT c = 0; c < 4; ++c) image[((y + oy) * aw + x + ox) * 4 + c] = value;
                    }
                double sum = 0;
                for (UINT y = 0; y < grid.second; ++y)
                    for (UINT x = 0; x < grid.first; ++x)
                    {
                        const auto sx = UINT(std::floor(double(x) / (grid.first - 1) * (w - 1) + .5));
                        const auto sy = UINT(std::floor(double(y) / (grid.second - 1) * (h - 1) + .5));
                        const double value = image[((sy + oy) * aw + sx + ox) * 4];
                        if (method == PE::Transformer2)
                            sum += std::isnan(value) ? 0. : std::clamp(value, 0., 10000.);
                        else if (value > 0 && std::isfinite(value)) sum += std::log(value + 1e-6);
                    }
                const auto mean = sum / (grid.first * grid.second);
                const auto expected = ReferenceHalf(method == PE::Transformer2 ? .23547087609767914 / std::max(mean, .005)
                                                                                 : (.18 / .82) * std::exp(-mean));
                const auto result = f.exposure(image, w, h, grid.first, grid.second, method, 1.f, true, false, 2.f, ox, oy);
                Near(result[0], expected, "nonuniform exposure"); Near(result[1], 2.f, "previous exposure"); ++checks;
            }
        for (UINT method : { PE::LegacyCNN, PE::LegacyK })
        {
            const auto held = f.exposure(std::vector<float>(65 * 37 * 4, 2000.f), 65, 37, 33, 19, method, 4.f, false, false, .25f);
            Near(held[0], .25f, "hold current"); Near(held[1], .25f, "hold previous"); ++checks;
        }
        // Abrupt dark/bright transition, changing PreExposure, reset on an odd evaluation.
        for (UINT method : { PE::LegacyCNN, PE::LegacyK, PE::Transformer2 })
        {
            float old = 1.f;
            for (UINT i = 0; i < 6; ++i)
            {
                const float value = i % 2 ? 2.f : .03125f, pre = i % 3 ? 4.f : 1.f;
                const bool reset = i == 0 || i == 3, measure = PE::Measure(method, i, reset);
                const auto result = f.exposure(std::vector<float>(65 * 37 * 4, value), 65, 37, 33, 19,
                                               method, pre, measure, reset, old);
                const double y = value / double(pre);
                const float expected = measure ? ReferenceHalf(method == PE::Transformer2
                    ? .23547087609767914 / value : (.18 / .82) * std::exp(-std::log(y + 1e-6))) : old;
                Near(result[0], expected, "temporal current");
                Near(result[1], reset ? (method == PE::Transformer2 ? 0.f : result[0]) : old, "temporal previous");
                old = result[0]; ++checks;
            }
        }
        // Exercise half RTZ around intervals where round-to-nearest would go upwards.
        for (double fraction : { .01, .49, .51, .99 })
        {
            const float y = float(.23547087609767914 / (Half(0x3b00) + fraction * (Half(0x3b01) - Half(0x3b00))));
            const auto result = f.exposure(std::vector<float>(64 * 32 * 4, y), 64, 32, 32, 16, PE::Transformer2, 1.f);
            if (result[0] != Half(0x3b00)) throw std::runtime_error("Exposure did not truncate FP16 toward zero");
            ++checks;
        }
        for (float value : { .000001f, .123456f, 1.f, 65504.f, -1.f, NAN })
        {
            f.r.begin(); std::vector<float> dummy(4, 1.f);
            auto in = f.texture(1, 1, &dummy), out = f.texture(1, 1, nullptr, DXGI_FORMAT_R32_FLOAT);
            DlssNr::Superior::DlssNrConstants c {}; c.Mode = 42; c.WhitePoint = value;
            f.dispatch(f.meter.Get(), c, in.Get(), in.Get(), out.Get(), 1, 1);
            Near(f.read(out.Get())[0], std::isfinite(value) && value > 0 ? value : 1.f, "manual exposure"); ++checks;
        }
        // Positive-only transport and its inverse: neutral, darkened, mixed channels,
        // >2 HDR carrier, lower clamp, invalid multiplier and finite colour fallback.
        for (float gain : { .01f, .125f, .5f, 1.f, 16.f, 1024.f, NAN })
        {
            const float safe = std::isfinite(gain) ? gain : 1.f;
            std::vector<float> proxy(17 * 9 * 4, .25f), answer(proxy.size());
            for (size_t i = 0; i < answer.size(); ++i)
                answer[i] = i % 4 == 0 ? .25f : i % 4 == 1 ? .125f : i % 4 == 2 ? .5f : -2.f;
            f.r.begin(); auto p = f.texture(17, 9, &proxy), a = f.texture(17, 9, &answer), out = f.texture(17, 9);
            DlssNr::Superior::DlssNrConstants c {}; c.Mode = 37; c.Width = 17; c.Height = 9; c.WhitePoint = gain;
            c.MaxRatio = 64;
            c.ResidualScale = 16;
            c.ResidualBlend = .1f;
            c.DirectResolveUpscaler = 7; // retired fields must be inert
            f.dispatch(f.carrier.Get(), c, p.Get(), a.Get(), out.Get(), 3, 2);
            auto encoded = f.read(out.Get());
            for (size_t i = 0; i < encoded.size(); i += 4)
            {
                Near(encoded[i], .5f, "neutral carrier"); Near(encoded[i + 1], .375f, "dark carrier");
                Near(encoded[i + 2], .5f + .25f * safe, "bright carrier");
            }
            f.r.begin(); auto e = f.texture(17, 9, &encoded), decoded = f.texture(17, 9);
            c.Mode = 38; f.dispatch(f.carrier.Get(), c, e.Get(), e.Get(), decoded.Get(), 3, 2);
            const auto values = f.read(decoded.Get());
            for (size_t i = 0; i < values.size(); i += 4)
            {
                Near(values[i], 0.f, "neutral decode"); Near(values[i + 1], -.125f, "dark decode");
                InverseNear(values[i + 2], .25f, safe, "bright decode");
            }
            ++checks;
        }
        // Rejected transport fields below are deliberately non-default: they must not affect colour.
        // Full-answer path: independent forward/inverse matrix references, varying current proxy,
        // raw-answer bypass, clipping, invalid RGB, both real FP16 and FP32 carrier writes, and a
        // synthetic DLSS change. Decode must use this proxy, never the original NR answer or 0.5.
        for (float gain : {.01f, .5f, 1.f, 16.f}) for (float saturation : {1.f, 4.f})
        for (float strength : {0.f, 1.f}) for (bool inverted : {false, true})
        for (UINT pair = 0; pair < 3; ++pair) for (bool halfCarrier : {false, true})
        for (bool filteredChange : {false, true})
        {
            constexpr UINT w = 17, h = 9;
            std::vector<float> proxy(w * h * 4), answer(proxy.size()), depth(proxy.size());
            for (size_t i = 0; i < proxy.size(); i += 4)
            {
                const unsigned pattern = unsigned(i / 4 % 7);
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    proxy[i + channel] = pattern == 5 ? 32.f + channel : .125f + float((i / 4 + channel) % 4) * .125f;
                    answer[i + channel] = pattern == 0 ? proxy[i + channel] :
                        pattern == 1 ? proxy[i + channel] + .0078125f * float(int(channel) - 1) :
                        pattern == 2 ? -1.f : pattern == 3 ? 8.f + channel : pattern == 4 ? NAN :
                        pattern == 5 ? proxy[i + channel] - .03125f : INFINITY;
                }
                proxy[i + 3] = answer[i + 3] = 1;
                depth[i] = float(i / 4 % 9) / 8.f;
            }
            f.r.begin(); auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer),
                d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                out = f.texture(w, h, nullptr, halfCarrier ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT);
            DlssNr::Superior::DlssNrConstants c {}; c.Mode = 37; c.Width = w; c.Height = h; c.WhitePoint = gain; c.MaxRatio = saturation;
            c.DirectDetailMode = 1; c.ResidualScale = strength; c.ResidualBlend = 1; c.SkinProtection = pair; c.Passthrough = inverted;
            f.dispatch(f.carrier.Get(), c, p.Get(), a.Get(), out.Get(), 3, 2, d.Get());
            auto encoded = f.read(out.Get());
            std::vector<std::array<double,3>> colours(w * h);
            for (size_t i = 0; i < encoded.size(); i += 4)
            {
                double t = inverted ? 1. - depth[i] : depth[i]; t = t * t * (3. - 2. * t);
                std::array<double,3> edit {};
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    const double n = std::isfinite(answer[i + channel]) ? answer[i + channel] : proxy[i + channel];
                    const double delta = n - proxy[i + channel];
                    edit[channel] = delta > 0 ? delta * gain : delta;
                    colours[i / 4][channel] = 0; // obsolete depth-colour slots are inert
                }
                const auto& chroma = edit;
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    const double expected = std::min(65504., std::clamp(double(proxy[i + channel]) + chroma[channel], 0., 65504.) + colours[i / 4][channel]);
                    Near(encoded[i + channel], float(expected), "full NR answer encode");
                    if (!halfCarrier && gain == 1.f && saturation == 1.f && strength == 0.f && std::isfinite(answer[i + channel]))
                        if (encoded[i + channel] != std::clamp(answer[i + channel], 0.f, 65504.f))
                            throw std::runtime_error("Full NR answer did not bypass edit encoding");
                    if (filteredChange) encoded[i + channel] += .00390625f * float(int(channel) - 1);
                    if (filteredChange && i / 4 % 7 == 4 && channel != 0)
                        encoded[i + channel] = channel == 1 ? NAN : INFINITY;
                }
            }
            f.r.begin(); auto e = f.texture(w, h, &encoded), decoded = f.texture(w, h);
            c.Mode = 38; f.dispatch(f.carrier.Get(), c, e.Get(), p.Get(), decoded.Get(), 3, 2, d.Get());
            const auto values = f.read(decoded.Get());
            for (size_t i = 0; i < encoded.size(); i += 4)
            {
                std::array<double,3> delta {};
                for (unsigned channel = 0; channel < 3; ++channel)
                    delta[channel] = std::isfinite(encoded[i + channel])
                        ? encoded[i + channel] - colours[i / 4][channel] - proxy[i + channel] : 0.;
                const auto& restored = delta;
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    const float expected = float(restored[channel] > 0 ? restored[channel] / gain : restored[channel]);
                    InverseNear(values[i + channel], expected, gain, "post-DLSS answer minus current proxy");
                    if (!filteredChange && i / 4 % 7 == 0)
                    {
                        const float peak = std::max({.5f, encoded[i], encoded[i + 1], encoded[i + 2]});
                        const float tolerance = halfCarrier ? (std::ldexp(1.f, std::ilogb(peak) - 10) + .000004f) / std::min(gain, 1.f) : .000002f;
                        if (std::abs(values[i + channel]) > tolerance)
                            throw std::runtime_error("Full-answer zero edit exceeded storage precision bound");
                    }
                }
            }
            ++checks;
        }
        for (float gain : {.01f, .125f, .5f, 1.f, 16.f, 1024.f, NAN})
        for (bool fullAnswer : {false, true})
        {
            constexpr UINT w = 17, h = 9;
            std::vector<float> proxy(w * h * 4, .25f), answer(proxy.size());
            for (size_t i = 0; i < answer.size(); ++i)
                answer[i] = i % 7 == 0 ? .25f : i % 7 == 1 ? -.5f : i % 7 == 2 ? 8.f :
                            i % 7 == 3 ? NAN : i % 7 == 4 ? INFINITY : i % 7 == 5 ? .5f : .125f;
            f.r.begin(); auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer), out = f.texture(w, h);
            DlssNr::Superior::DlssNrConstants c {}; c.Mode = 43; c.Width = w; c.Height = h; c.WhitePoint = gain;
            c.DirectDetailMode = fullAnswer; c.MaxRatio = 64; c.ResidualScale = 16; // must not affect the snapshot
            f.dispatch(f.carrier.Get(), c, p.Get(), a.Get(), out.Get(), 3, 2);
            const auto values = f.read(out.Get());
            const float safeGain = std::isfinite(gain) ? gain : 1.f;
            for (size_t i = 0; i < values.size(); ++i)
            {
                const float edit = std::isfinite(answer[i]) ? answer[i] - proxy[i] : 0.f;
                const float expected = i % 4 == 3 ? 1.f : edit <= 0 ? edit : edit * safeGain;
                Near(values[i], expected, "snapshot signed edit after positive gain");
                if (i % 4 != 3 && edit == 0 && values[i] != 0)
                    throw std::runtime_error("Snapshot neutral edit must be exactly zero");
            }
            // End-to-end GPU -> readback -> DDS writer contract, preserving negative/HDR words exactly.
            std::vector<char> dds;
            if (!DlssNr::EditDump::WriteRows(w, h, values.data(), size_t(w) * 16, [&](const void* data, size_t bytes)
                { dds.insert(dds.end(), static_cast<const char*>(data), static_cast<const char*>(data) + bytes); return true; }))
                throw std::runtime_error("GPU snapshot DDS serialization failed");
            if (dds.size() != 148 + values.size() * 4 || std::memcmp(dds.data() + 148, values.data(), values.size() * 4))
                throw std::runtime_error("GPU snapshot DDS did not preserve signed FP32 data");
            ++checks;
        }

        std::cout << "Private exposure/carrier GPU PASS: " << checks << " cases against independent numeric reference\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
