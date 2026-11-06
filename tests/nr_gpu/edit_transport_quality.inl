#include "../../OptiScaler/dlssnr/DlssNr_AutoGain.h"
#include "../../OptiScaler/dlssnr/DlssNr_EditDumpMetadata.h"
void EditTransportPolicyQuality()
{
    DlssNr::AutoGain::Controller capped;
    DlssNr::GainPreview::Status e;
    e.valid = true;
    e.paused = false;
    e.measurements = 4;
    e.measuredAt = 1;
    e.smoothed = 20;
    const void* device = &capped;
    if (capped.Update(true, true, false, 40, e, device, 1, 1, 6) != 6 ||
        capped.Update(true, true, true, 40, e, device, 1, 1.1, 3) != 3 ||
        capped.Update(true, false, true, 40, e, device, 1, 1.2, 2) != 2 ||
        capped.Update(false, true, false, 40, e, device, 1, 1.3, 2) != 40)
        throw std::runtime_error("Automatic ceiling/manual preservation/held enforcement failed");
    if (DlssNr::EditTransport::AutoMaximum(NAN) != 1024 || DlssNr::EditTransport::AutoMaximum(-1) != 1 ||
        DlssNr::EditTransport::EffectiveMode(2, false) != 2 || DlssNr::EditTransport::EffectiveMode(1, true) != 0 ||
        DlssNr::EditTransport::SanitizeMode(99) != 0)
        throw std::runtime_error("Transport configuration fallback failed");
    DlssNr::EditDump::Metadata m;
    m.transportMode = 1;
    m.transportParametersIncluded = true;
    if (DlssNr::EditDump::MetadataJson(m).find("\"positive_gain\": null") == std::string::npos ||
        DlssNr::EditDump::MetadataJson(m).find(".transport.dds") == std::string::npos)
        throw std::runtime_error("GPU transport metadata would misreport the CPU preference as applied gain");
    m.transportMode = 2;
    if (DlssNr::EditDump::MetadataJson(m).find("shifted_nonnegative_dlss_input") == std::string::npos)
        throw std::runtime_error("Shift capture units missing");
}
struct TransportFixture
{
    Fixture& f;
    UINT w, h, bw, bh;
    ComPtr<ID3D12Resource> proxy, answer, parts, coefficients;
    ComPtr<ID3D12PipelineState> bounds, reduce, carrier;
    TransportFixture(Fixture& fixture, UINT width, UINT height, const std::vector<float>& p,
                     const std::vector<float>& a)
        : f(fixture), w(width), h(height), bw((w + 31) / 32), bh((h + 31) / 32)
    {
        bounds = f.visibilityPipeline("private_transport_bounds");
        reduce = f.visibilityPipeline("private_transport_reduce");
        carrier = f.visibilityPipeline("private_affine_base");
        f.r.begin();
        proxy = f.texture(w, h, &p);
        answer = f.texture(w, h, &a);
        parts = f.texture(bw, bh, nullptr, DXGI_FORMAT_R32G32_FLOAT);
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 256;
        desc.Height = 1;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        coefficients = f.r.resource(desc, D3D12_HEAP_TYPE_DEFAULT, UAV);
    }
    std::array<float, 4> Prepare(unsigned mode, float gain)
    {
        DlssNr::Superior::DlssNrConstants c {};
        c.Mode = 60;
        c.Width = w;
        c.Height = h;
        c.DebugView = mode;
        c.WhitePoint = gain;
        f.dispatch(bounds.Get(), c, proxy.Get(), answer.Get(), parts.Get(), bw, bh);
        f.r.barrier(parts.Get(), UAV, SRV);
        c.Mode = 61;
        c.Width = bw;
        c.Height = bh;
        f.dispatch(reduce.Get(), c, parts.Get(), parts.Get(), coefficients.Get(), 1, 1);
        auto rb = f.r.buffer(256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        f.r.barrier(coefficients.Get(), UAV, D3D12_RESOURCE_STATE_COPY_SOURCE);
        f.r.list->CopyBufferRegion(rb.Get(), 0, coefficients.Get(), 0, 16);
        f.r.barrier(coefficients.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        f.r.finish();
        float* p;
        check(rb->Map(0, nullptr, reinterpret_cast<void**>(&p)));
        std::array<float, 4> value;
        std::copy(p, p + 4, value.begin());
        rb->Unmap(0, nullptr);
        f.r.begin();
        return value;
    }
};
void CheckTransportSnapshot(TransportFixture& t, const std::array<float, 4>& expected)
{
    auto& f = t.f;
    auto image = f.texture(1, 1);
    f.r.barrier(image.Get(), UAV, D3D12_RESOURCE_STATE_COPY_DEST);
    f.r.barrier(t.coefficients.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
    src.pResource = t.coefficients.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint = { DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, 1, 256 };
    dst.pResource = image.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    f.r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    f.r.barrier(t.coefficients.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    f.r.barrier(image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, UAV);
    auto got = f.read(image.Get());
    for (unsigned c = 0; c < 4; ++c)
        if (got[c] != expected[c])
            throw std::runtime_error("1x1 GPU transport coefficient snapshot mismatch");
    f.r.begin();
}
void EditTransportQuality(Fixture& f)
{
    EditTransportPolicyQuality();
    using namespace DlssNr::Superior;
    unsigned checks = 0;
    for (unsigned mode : { 1u, 2u })
        for (float requested : { .01f, .5f, 1.f, 6.f, 64.f })
            for (unsigned scene : { 0u, 1u, 2u, 3u, 4u, 5u, 6u })
            {
                const UINT w = 37, h = 19;
                std::vector<float> p(w * h * 4, .25f), a(p), raw(p.size(), 0);
                float minimum = 3.402823466e38f, maximum = -minimum;
                for (size_t i = 0; i < p.size(); i += 4)
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        float e = scene == 2   ? 0.f
                                  : scene == 3 ? .015625f
                                               : (c == 0   ? .03125f
                                                  : c == 1 ? -.015625f
                                                           : .0625f);
                        if (scene == 6)
                            e = .000244140625f;
                        if (i == raw.size() - 4 && c == 2)
                            e = scene == 0   ? -.0625f
                                : scene == 1 ? -1.f
                                : scene == 4 ? -65504.f
                                : scene == 6 ? -.000244140625f
                                             : e;
                        if (scene == 5 && i == 0 && c == 0)
                            e = 65503.75f;
                        a[i + c] = p[i + c] + e;
                        if (scene == 4 && i == 0 && c == 0)
                        {
                            p[i + c] = NAN;
                            a[i + c] = INFINITY;
                            e = 0;
                        }
                        float proxy = std::isfinite(p[i + c]) ? p[i + c] : 0,
                              answer = std::isfinite(a[i + c]) ? a[i + c] : proxy;
                        e = answer - proxy;
                        if (!std::isfinite(e))
                            e = 0;
                        raw[i + c] = e;
                        minimum = std::min(minimum, e);
                        maximum = std::max(maximum, e);
                    }
                TransportFixture t(f, w, h, p, a);
                auto gpu = t.Prepare(mode, requested);
                if (mode == 1 && requested == 1.f && scene == 0)
                    CheckTransportSnapshot(t, gpu);
                float gain = requested;
                if (mode == 1)
                {
                    gain = minimum < 0 ? .5f / -minimum : 1.f;
                    if (maximum > 0)
                        gain = std::min(gain, 65503.5f / maximum);
                }
                if (scene == 6 && mode == 1 && gpu[0] <= 1024)
                    throw std::runtime_error("Symmetric mode incorrectly inherited positive-only gain limit");
                Near(gpu[0], gain, "shared transport gain");
                Near(gpu[2], minimum, "all-channel exact minimum");
                Near(gpu[3], maximum, "all-channel maximum");
                DlssNr::Superior::DlssNrConstants c {};
                c.Mode = 37;
                c.Width = w;
                c.Height = h;
                Fields::SetCarrierTransport(c, mode);
                auto encoded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
                     decoded = f.texture(w, h, nullptr, DXGI_FORMAT_R32G32B32A32_FLOAT);
                f.dispatch(t.carrier.Get(), c, t.proxy.Get(), t.answer.Get(), encoded.Get(), (w + 7) / 8, (h + 7) / 8,
                           nullptr, nullptr, nullptr, nullptr, nullptr, 0, true, nullptr, t.coefficients.Get());
                auto storedValues = f.read(encoded.Get());
                f.r.begin();
                f.r.barrier(encoded.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, SRV);
                c.Mode = 38;
                f.dispatch(t.carrier.Get(), c, encoded.Get(), t.proxy.Get(), decoded.Get(), (w + 7) / 8, (h + 7) / 8,
                           nullptr, nullptr, nullptr, nullptr, nullptr, 0, true, nullptr, t.coefficients.Get());
                auto values = f.read(decoded.Get());
                for (size_t i = 0; i < values.size(); i += 4)
                    for (unsigned channel = 0; channel < 3; ++channel)
                    {
                        double e = raw[i + channel];
                        double enc = mode == 1 ? .5 + e * gpu[0] : (e - minimum) * gpu[0];
                        enc = std::clamp(enc, 0., 65504.);
                        float stored = storedValues[i + channel];
                        if (!std::isfinite(stored) || stored < 0 || stored > 65504 ||
                            std::abs(double(stored) - enc) > std::max(1e-7, std::abs(enc) * .0011))
                            throw std::runtime_error("Shared affine encoder exceeded an FP16 interval");
                        double expected =
                            mode == 1 ? (double(stored) - .5) / gpu[0] : double(stored) / gpu[0] + minimum;
                        const double roundoff =
                            2e-6 + 4 * std::numeric_limits<float>::epsilon() *
                                       (std::abs(double(stored) / gpu[0]) + std::abs(double(minimum)));
                        if (!std::isfinite(values[i + channel]) ||
                            std::abs(values[i + channel] - expected) > std::max(roundoff, std::abs(expected) * .000001))
                            throw std::runtime_error("Shared RGB inverse mode=" + std::to_string(mode) +
                                                     " scene=" + std::to_string(scene));
                    }
                ++checks;
            }
    std::cout
        << "Edit transport bounds/roundtrip PASS: " << checks
        << " shared-RGB Symmetric/Shift, signs, constant edit, HDR, NaN/Inf, exact odd-grid extrema and FP16 cases\n";
}
void AffineGhostQuality(Fixture& f)
{
    using namespace DlssNr::Superior;
    unsigned checks = 0;
    for (float global : { 0.f, .25f })
        for (unsigned mode : { 1u, 2u })
            for (unsigned method = 0; method <= 5; ++method)
                for (bool contrast : { false, true })
                    for (unsigned metric : { 0u, 1u })
                        for (float referenceDelta : { 0.f, .015625f })
                        {
                            if (contrast && method < 2)
                                continue;
                            const UINT w = 16, h = 8, step = method == 5 ? 4 : 2, mw = w / step, mh = h / step;
                            const float raw[3] = { .0625f, -.03125f, .015625f },
                                        filtered[3] = { .09375f, -.015625f, .0625f };
                            std::vector<float> p(w * h * 4, .25f), a(p), after(p), depth(p.size(), .01f),
                                mv(p.size(), 0), state(mw * mh * 4, 0), ref(state);
                            for (size_t i = 0; i < p.size(); i += 4)
                                for (unsigned c = 0; c < 3; ++c)
                                    a[i + c] += raw[c];
                            TransportFixture t(f, w, h, p, a);
                            auto coeff = t.Prepare(mode, 6);
                            for (size_t i = 0; i < after.size(); i += 4)
                                for (unsigned c = 0; c < 3; ++c)
                                    after[i + c] =
                                        mode == 1 ? .5f + filtered[c] * coeff[0] : (filtered[c] - coeff[2]) * coeff[0];
                            for (size_t i = 0; i < state.size(); i += 4)
                            {
                                if (method >= 3 && method < 5)
                                {
                                    float z = .01f;
                                    uint32_t bits;
                                    std::memcpy(&bits, &z, 4);
                                    bits = (bits & 0xffffff80u) | 4;
                                    std::memcpy(&state[i], &bits, 4);
                                }
                                else
                                {
                                    state[i] = method == 5 ? 1 : .01f;
                                    state[i + 1] = 4;
                                }
                                for (unsigned c = 0; c < 3; ++c)
                                    ref[i + c] = raw[c] - (c == 1 ? referenceDelta : 0);
                                ref[i + 3] = 1;
                            }
                            auto e = f.texture(w, h, &after, DXGI_FORMAT_R16G16B16A16_FLOAT),
                                 d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                                 motion = f.texture(w, h, &mv, DXGI_FORMAT_R32G32_FLOAT),
                                 hist = f.texture(mw, mh, &state,
                                                  method == 5   ? DXGI_FORMAT_R16_FLOAT
                                                  : method >= 3 ? DXGI_FORMAT_R32G32_UINT
                                                                : DXGI_FORMAT_R32G32B32A32_FLOAT),
                                 reference = f.texture(mw, mh, &ref, DXGI_FORMAT_R16G16B16A16_FLOAT),
                                 out = f.texture(w, h);
                            DlssNr::Superior::DlssNrConstants c {};
                            c.Mode = 38;
                            c.Width = w;
                            c.Height = h;
                            Fields::SetCarrierTransport(c, mode);
                            Fields::SetCarrierDepthConvention(c, true);
                            DlssNr::GhostRejection::Settings settings { true, .01f, .02f, .75f, false, method };
                            settings.historyFade = 0;
                            settings.contrastEnabled = contrast;
                            settings.contrastMetric = metric;
                            settings.contrastThreshold = .05f;
                            settings.contrastSoftness = 0;
                            Fields::SetCarrierGhostRejection(c, settings);
                            Fields::SetUnfilteredEditWeight(c, global);
                            Fields::SetGhostDepthPrecision(c, 0);
                            std::string kind = method == 0   ? "ghost"
                                               : method == 1 ? "guided"
                                               : method == 2 ? "temporal"
                                               : method == 3 ? "simple"
                                               : method == 4 ? (global > 0 ? "fast_fade" : "fast")
                                                             : "coarse";
                            if (contrast)
                                kind = "contrast_" + (method == 4 ? std::string("fast") : kind);
                            auto ps = f.visibilityPipeline("private_affine_" + kind);
                            f.dispatch(ps.Get(), c, e.Get(), contrast ? reference.Get() : t.proxy.Get(), out.Get(), 2,
                                       1, d.Get(), t.proxy.Get(), t.answer.Get(),
                                       method == 1 ? motion.Get() : hist.Get(), nullptr, 0, true, nullptr,
                                       t.coefficients.Get());
                            auto got = f.read(out.Get());
                            const double distance = referenceDelta * coeff[0] * (metric == 1 ? .7152 : 1.);
                            const float rejection = method == 1 || contrast && distance > .05 ? 0 : .75f,
                                        total = global + (1 - global) * rejection;
                            for (size_t i = 0; i < got.size(); i += 4)
                            {
                                Near(got[i + 3], rejection * (1 - global), "affine rejection alpha/globalfade");
                                for (unsigned ch = 0; ch < 3; ++ch)
                                    Near(got[i + ch], filtered[ch] * (1 - total) + raw[ch] * total,
                                         "affine inverse/mask/raw blend");
                            }
                            ++checks;
                        }
    std::cout
        << "Affine ghost integration PASS: " << checks
        << " legacy/guided/temporal/compact/coarse/negative-channel contrast, RGB/brightness and globalfade cases\n";
}
void EditTransportTiming(Fixture& f, const std::string& stem)
{
    unsigned w, h;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h);
    std::vector<float> proxy(raw.size(), .5f), answer(proxy), zero(raw.size(), 0);
    for (size_t i = 0; i < raw.size(); i += 4)
        for (unsigned c = 0; c < 3; ++c)
            answer[i + c] += raw[i + c];
    const UINT bw = (w + 31) / 32, bh = (h + 31) / 32;
    auto meter = f.visibilityPipeline("private_transport_bounds"),
         reduce = f.visibilityPipeline("private_transport_reduce"),
         affine = f.visibilityPipeline("private_affine_base");
    auto original = f.r.pipeline("artifacts/transport-modes/baseline", "private_carrier");
    f.r.begin();
    auto p = f.texture(w, h, &proxy, DXGI_FORMAT_R16G16B16A16_FLOAT),
         a = f.texture(w, h, &answer, DXGI_FORMAT_R16G16B16A16_FLOAT),
         mv = f.texture(w, h, &zero, DXGI_FORMAT_R32G32_FLOAT);
    struct Variant
    {
        ComPtr<ID3D12Resource> parts, coeff, encoded, decoded, motion;
        bool used = false;
    };
    Variant v[3];
    for (unsigned k = 0; k < 3; ++k)
    {
        v[k].encoded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
        v[k].decoded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
        v[k].motion = f.texture(w, h, nullptr, DXGI_FORMAT_R32G32_FLOAT);
        if (k)
        {
            v[k].parts = f.texture(bw, bh, nullptr, DXGI_FORMAT_R32G32_FLOAT);
            D3D12_RESOURCE_DESC d {};
            d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            d.Width = 256;
            d.Height = 1;
            d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
            d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            v[k].coeff = f.r.resource(d, D3D12_HEAP_TYPE_DEFAULT, UAV);
        }
    }
    ComPtr<ID3D12QueryHeap> queries;
    D3D12_QUERY_HEAP_DESC q {};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 2;
    check(f.r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
    auto rb = f.r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency;
    check(f.r.queue->GetTimestampFrequency(&frequency));
    f.r.finish();
    f.uploads.clear();
    auto measure = [&](unsigned mode)
    {
        auto& s = v[mode];
        f.r.begin();
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (s.used)
        {
            f.r.barrier(s.encoded.Get(), SRV, UAV);
            f.r.barrier(s.decoded.Get(), SRV, UAV);
            f.r.barrier(s.motion.Get(), SRV, UAV);
        }
        DlssNr::Superior::DlssNrConstants c {};
        if (mode)
        {
            if (s.used)
            {
                f.r.barrier(s.parts.Get(), SRV, UAV);
                f.r.barrier(s.coeff.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, UAV);
            }
            c.Mode = 60;
            c.Width = w;
            c.Height = h;
            c.DebugView = mode;
            c.WhitePoint = mode == 2 ? 6.f : 1.f;
            f.dispatch(meter.Get(), c, p.Get(), a.Get(), s.parts.Get(), bw, bh, nullptr, nullptr, nullptr, nullptr,
                       nullptr, 0, false);
            f.r.barrier(s.parts.Get(), UAV, SRV);
            c.Mode = 61;
            c.Width = bw;
            c.Height = bh;
            f.dispatch(reduce.Get(), c, s.parts.Get(), s.parts.Get(), s.coeff.Get(), 1, 1, nullptr, nullptr, nullptr,
                       nullptr, nullptr, 1, false);
            f.r.barrier(s.coeff.Get(), UAV, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        }
        c = {};
        c.Mode = 39;
        c.Width = w;
        c.Height = h;
        c.WhitePoint = 1;
        c.TransferStrength = float(w);
        c.ColourStrength = float(h);
        c.MvScaleX = c.MvScaleY = 1;
        DlssNr::Superior::Fields::SetCarrierTransport(c, mode);
        f.dispatch(mode ? affine.Get() : original.Get(), c, p.Get(), a.Get(), s.encoded.Get(), (w + 7) / 8, (h + 7) / 8,
                   nullptr, mv.Get(), nullptr, nullptr, nullptr, 2, false, s.motion.Get(),
                   mode ? s.coeff.Get() : nullptr);
        f.r.barrier(s.encoded.Get(), UAV, SRV);
        f.r.barrier(s.motion.Get(), UAV, SRV);
        c.Mode = 38;
        f.dispatch(mode ? affine.Get() : original.Get(), c, s.encoded.Get(), p.Get(), s.decoded.Get(), (w + 7) / 8,
                   (h + 7) / 8, nullptr, nullptr, nullptr, nullptr, nullptr, 3, false, nullptr,
                   mode ? s.coeff.Get() : nullptr);
        f.r.barrier(s.decoded.Get(), UAV, SRV);
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        f.r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, rb.Get(), 0);
        f.r.finish();
        f.uploads.clear();
        s.used = true;
        UINT64* times;
        check(rb->Map(0, nullptr, reinterpret_cast<void**>(&times)));
        double ms = double(times[1] - times[0]) * 1000 / frequency;
        rb->Unmap(0, nullptr);
        return ms;
    };
    std::ofstream out("artifacts/transport-modes/timing.csv");
    out << "comparison,block,position,variant,width,height,ms\n";
    for (unsigned candidate : { 1u, 2u })
    {
        for (unsigned warm = 0; warm < 16; ++warm)
        {
            measure(0);
            measure(candidate);
        }
        for (unsigned block = 0; block < 64; ++block)
            for (unsigned pos = 0; pos < 4; ++pos)
            {
                unsigned which = pos == 1 || pos == 2 ? candidate : 0;
                out << candidate << ',' << block << ',' << pos << ',' << which << ',' << w << ',' << h << ','
                    << measure(which) << '\n';
            }
    }
    std::cout << "Edit transport whole-region timestamps PASS: 16-pair warmup, 64 ABBA blocks, baseline/2 modes, " << w
              << 'x' << h << "\n";
}
