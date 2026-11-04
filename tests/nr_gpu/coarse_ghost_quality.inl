// Independent max-filter/fade reference, and controlled visibility sequences on the actual quarter grid.
void CoarseGhostQuality(Fixture& f)
{
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    auto d1 = f.r.pipeline(dir, "private_ghost_coarse_dilate_1"),
         d2 = f.r.pipeline(dir, "private_ghost_coarse_dilate_2"), editPs = f.r.pipeline(dir, "private_carrier_coarse"),
         answerPs = f.r.pipeline(dir, "private_carrier_coarse_answer");
    unsigned checks = 0;
    for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 3, 5 }, { 17, 9 }, { 65, 33 } })
        for (unsigned radius : { 1u, 2u })
            for (float fade : { 0.f, 3.f, 120.f, NAN, -1.f })
            {
                const unsigned w = size.first, h = size.second;
                std::vector<float> state(w * h * 4, 0);
                std::vector<unsigned> ages(w * h);
                for (unsigned i = 0; i < w * h; ++i)
                {
                    ages[i] = (i % 7 == 0) ? 0 : (i * 13 + 3) % 121;
                    float z = .01f;
                    uint32_t bits;
                    std::memcpy(&bits, &z, 4);
                    bits = (bits & 0xffffff80u) | ages[i];
                    std::memcpy(&state[i * 4], &bits, 4);
                }
                f.r.begin();
                auto source = f.texture(w, h, &state, DXGI_FORMAT_R32G32_UINT),
                     field = f.texture(w, h, nullptr, DXGI_FORMAT_R16_FLOAT);
                DlssNr::Superior::DlssNrConstants c {};
                DlssNr::GhostRejection::Settings settings {};
                settings.coarseRadius = radius;
                settings.historyFade = 3;
                DlssNr::Superior::Fields::SetCoarseGhostDilate(c, w, h, settings);
                c.TransferStrength = fade; // exercise the shader's invalid-parameter fallback independently
                f.dispatch(radius == 2 ? d2.Get() : d1.Get(), c, source.Get(), source.Get(), field.Get(), (w + 7) / 8,
                           (h + 7) / 8);
                auto got = f.read(field.Get());
                std::vector<double> expected(w * h);
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x)
                    {
                        unsigned a = 0;
                        for (int yy = -int(radius); yy <= int(radius); ++yy)
                            for (int xx = -int(radius); xx <= int(radius); ++xx)
                                a = std::max(a, ages[std::clamp(int(y) + yy, 0, int(h) - 1) * w +
                                                     std::clamp(int(x) + xx, 0, int(w) - 1)]);
                        const double ft = std::isfinite(fade) ? std::clamp(double(fade), 0., 120.) : 3.;
                        const double value = ft > 0 ? std::min(double(a) / ft, 1.) : a > 0 ? 1 : 0;
                        expected[y * w + x] = got[(y * w + x) * 4];
                        if (!std::isfinite(expected[y * w + x]) || expected[y * w + x] < value - 1e-7 ||
                            expected[y * w + x] > value + .0005)
                            throw std::runtime_error("Coarse max dilation / upward FP16 reference");
                    }
                for (bool full : { false, true })
                    for (bool reversed : { false, true })
                    {
                        const unsigned fw = w * 4 - 1, fh = h * 4 - 1;
                        std::vector<float> proxy(fw * fh * 4, .2f), answer(proxy), encoded(proxy),
                            depth(proxy.size(), 0);
                        const float raw[3] = { .01f, -.02f, .03f }, filtered[3] = { .08f, -.04f, .12f };
                        for (unsigned y = 0; y < fh; ++y)
                            for (unsigned x = 0; x < fw; ++x)
                            {
                                const size_t i = (y * fw + x) * 4;
                                depth[i] = (x + y) % 19 == 0 ? NAN : reversed ? .01f : .99f;
                                for (unsigned ch = 0; ch < 3; ++ch)
                                {
                                    answer[i + ch] = proxy[i + ch] + raw[ch];
                                    encoded[i + ch] =
                                        (full ? proxy[i + ch] : .5f) + filtered[ch] * (filtered[ch] > 0 ? 6.f : 1.f);
                                }
                            }
                        f.r.begin();
                        f.r.barrier(field.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, SRV);
                        auto p = f.texture(fw, fh, &proxy), a = f.texture(fw, fh, &answer),
                             e = f.texture(fw, fh, &encoded), z = f.texture(fw, fh, &depth, DXGI_FORMAT_R32_FLOAT),
                             out = f.texture(fw, fh);
                        c = {};
                        c.Mode = 38;
                        c.Width = fw;
                        c.Height = fh;
                        DlssNr::Superior::Fields::SetPositiveCarrierGain(c, 6);
                        DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
                        DlssNr::Superior::Fields::SetCarrierDepthConvention(c, reversed);
                        settings.enabled = true;
                        settings.method = 5;
                        settings.strength = .75f;
                        DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
                        DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
                        f.dispatch(full ? answerPs.Get() : editPs.Get(), c, e.Get(), p.Get(), out.Get(), (fw + 7) / 8,
                                   (fh + 7) / 8, z.Get(), p.Get(), a.Get(), field.Get());
                        auto values = f.read(out.Get());
                        for (unsigned y = 0; y < fh; ++y)
                            for (unsigned x = 0; x < fw; ++x)
                            {
                                const size_t i = (y * fw + x) * 4;
                                const double px = (x + .5) * w / fw - .5, py = (y + .5) * h / fh - .5;
                                const int bx = int(std::floor(px)), by = int(std::floor(py));
                                double sample = 0;
                                for (int yy = 0; yy < 2; ++yy)
                                    for (int xx = 0; xx < 2; ++xx)
                                        sample += expected[std::clamp(by + yy, 0, int(h) - 1) * w +
                                                           std::clamp(bx + xx, 0, int(w) - 1)] *
                                                  (xx ? px - bx : 1 - (px - bx)) * (yy ? py - by : 1 - (py - by));
                                const double mask = std::isfinite(depth[i]) ? .75 * sample : 0;
                                // Hardware bilinear subtexel precision is part of the chosen approximation.
                                if (!std::isfinite(values[i + 3]) || std::abs(values[i + 3] - mask * .75) > .008)
                                    throw std::runtime_error("Coarse soft fringe independent reference");
                                for (unsigned ch = 0; ch < 3; ++ch)
                                {
                                    const double value =
                                        .75 * ((1 - mask) * filtered[ch] + mask * raw[ch]) + .25 * raw[ch];
                                    if (!std::isfinite(values[i + ch]) || std::abs(values[i + ch] - value) > .0015)
                                        throw std::runtime_error("Coarse raw/filtered composition reference");
                                }
                            }
                        f.r.begin();
                        f.r.barrier(field.Get(), SRV, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        f.r.finish();
                        ++checks;
                    }
            }
    // HDR uses real binary16 carrier/proxy/NR textures; the reference starts from their quantized values.
    for (bool full : { false, true })
        for (float gain : { .5f, 6.f })
        {
            constexpr unsigned w = 3, h = 5;
            std::vector<float> proxy(w * h * 4, full ? 20000.f : 0.f), answer(proxy), encoded(proxy),
                depth(w * h * 4, 0), maskImage(4, .5f);
            const float raw[3] = { 8000, -.0625f, 10000 }, filtered[3] = { 32000, -.125f, 16000 };
            for (size_t i = 0; i < proxy.size(); i += 4)
            {
                depth[i] = .01f;
                for (unsigned ch = 0; ch < 3; ++ch)
                {
                    answer[i + ch] = proxy[i + ch] + raw[ch];
                    encoded[i + ch] = std::clamp(
                        (full ? proxy[i + ch] : .5f) + filtered[ch] * (filtered[ch] > 0 ? gain : 1.f), 0.f, 65504.f);
                }
            }
            f.r.begin();
            auto p = f.texture(w, h, &proxy, DXGI_FORMAT_R16G16B16A16_FLOAT),
                 a = f.texture(w, h, &answer, DXGI_FORMAT_R16G16B16A16_FLOAT),
                 e = f.texture(w, h, &encoded, DXGI_FORMAT_R16G16B16A16_FLOAT),
                 z = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                 field = f.texture(1, 1, &maskImage, DXGI_FORMAT_R16_FLOAT), out = f.texture(w, h);
            DlssNr::Superior::DlssNrConstants c {};
            c.Mode = 38;
            c.Width = w;
            c.Height = h;
            DlssNr::Superior::Fields::SetPositiveCarrierGain(c, gain);
            DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
            DlssNr::Superior::Fields::SetCarrierDepthConvention(c, true);
            DlssNr::GhostRejection::Settings settings {};
            settings.enabled = true;
            settings.method = 5;
            settings.strength = .75f;
            DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
            DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
            f.dispatch(full ? answerPs.Get() : editPs.Get(), c, e.Get(), p.Get(), out.Get(), 1, 1, z.Get(), p.Get(),
                       a.Get(), field.Get());
            auto got = f.read(out.Get());
            const auto stored = [](float value)
            {
                const auto bits = DirectX::PackedVector::XMConvertFloatToHalf(value);
                return (bits & 0x8000u ? -1.f : 1.f) * Half(bits & 0x7fffu);
            };
            for (size_t i = 0; i < got.size(); i += 4)
            {
                for (unsigned ch = 0; ch < 3; ++ch)
                {
                    const float pr = stored(proxy[i + ch]);
                    const float d = stored(encoded[i + ch]) - (full ? pr : .5f);
                    const double decoded = std::min(d, 0.f) + double(std::max(d, 0.f)) / gain;
                    const double nr = stored(answer[i + ch]) - pr;
                    const double expected = (decoded * .625 + nr * .375) * .75 + nr * .25;
                    if (!std::isfinite(got[i + ch]) ||
                        std::abs(got[i + ch] - expected) > std::max(.001, std::abs(expected) * 1e-6))
                        throw std::runtime_error(
                            "Coarse HDR reference full=" + std::to_string(full) + " gain=" + std::to_string(gain) +
                            " ch=" + std::to_string(ch) + " got=" + std::to_string(got[i + ch]) +
                            " expected=" + std::to_string(expected) + " alpha=" + std::to_string(got[i + 3]));
                }
                if (got[i + 3] != .28125f)
                    throw std::runtime_error("Coarse HDR mask alpha");
            }
            ++checks;
        }

    for (bool reversed : { false, true })
        for (unsigned bits : { 0u, 16u, 24u })
        {
            TemporalGhostFixture t(f, 128, 64, true, false, 4);
            auto first = t.Frame(40, 4, .0102f, 0, -4, false, reversed, false, bits);
            for (size_t i = 1; i < first.size(); i += 4)
                if (first[i] != 0)
                    throw std::runtime_error("Coarse history warmup");
            auto opened = t.Frame(44, 4, .0102f, 0, -4, false, reversed, false, bits);
            if (opened[(6 * t.mw + 10) * 4 + 1] != 4)
                throw std::runtime_error("Coarse contact disocclusion window");
            if (opened[(6 * t.mw + 11) * 4 + 1] != 0)
                throw std::runtime_error("Coarse occluding foot must remain unmasked in history");
            for (unsigned frame = 1; frame <= 4; ++frame)
            {
                auto moved = t.Frame(999, 0, .03f, 4, 4, false, reversed, false, bits);
                if (moved[(6 * t.mw + 10 - frame) * 4 + 1] != float(4 - frame))
                    throw std::runtime_error("Coarse MV-follow and exact expiry");
            }
            auto reset = t.Frame(0, 0, .03f, 0, 0, true, reversed, false, bits);
            for (size_t i = 1; i < reset.size(); i += 4)
                if (reset[i] != 0)
                    throw std::runtime_error("Coarse reset");
            ++checks;
        }
    for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 17, 9 }, { 127, 63 } })
    {
        TemporalGhostFixture t(f, size.first, size.second, true, false, 4);
        t.Frame(0, 0, .03f, 0, 0);
        auto frame = t.Frame(0, 0, .03f, 12, 12);
        for (size_t i = 1; i < frame.size(); i += 4)
            if (frame[i] != 0)
                throw std::runtime_error("Coarse uniform camera / edges");
        auto invalid = t.Frame(0, 0, .03f, 0, 0, false, true, true);
        for (size_t i = 1; i < invalid.size(); i += 4)
            if (invalid[i] != 0)
                throw std::runtime_error("Coarse invalid guides");
        ++checks;
    }
    std::cout << "Coarse soft ghost mask PASS: " << checks
              << " dilation/FP16/filter/composition/contact/MV/expiry/reset/depth cases\n";
}

void CoarseGhostTiming(Fixture& f, const std::string& stem, unsigned radius, bool residual = false)
{
    const bool revision = true, retirement = true;
    const float gMathGain = 1, gMathBlend = 0;
    const std::string trial = "OptiScaler/shaders/dlssnr/superior/precompile";
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || w != dw || w != mw || h != ah || h != dh || h != mh)
        throw std::runtime_error("Compact timing dimensions");
    for (size_t i = 0; i < after.size(); i += 4)
        for (unsigned ch = 0; ch < 3; ++ch)
            if (after[i + ch] > 0)
                after[i + ch] *= gMathGain;
    if (residual)
        for (size_t i = 0; i < after.size(); i += 4)
            for (unsigned ch = 0; ch < 3; ++ch)
                after[i + ch] += .5f;
    std::vector<float> zero(raw.size(), 0);
    f.r.begin();
    auto r = f.texture(w, h, &raw, DXGI_FORMAT_R16G16B16A16_FLOAT),
         a = f.texture(w, h, &after, DXGI_FORMAT_R16G16B16A16_FLOAT),
         p = f.texture(w, h, &zero, DXGI_FORMAT_R16G16B16A16_FLOAT), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
         m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT);
    ComPtr<ID3D12Resource> outputs[2] = { f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
                                          f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT) };
    mw = (w + 1) / 2;
    mh = (h + 1) / 2;
    struct State
    {
        ComPtr<ID3D12Resource> recon, state[2], field;
        unsigned index = 0;
        bool valid = false;
    };
    State states[2];
    for (unsigned k = 0; k < 2; ++k)
    {
        auto& s = states[k];
        const unsigned stride = k ? 4 : 2;
        const unsigned mw = (w + stride - 1) / stride, mh = (h + stride - 1) / stride;
        s.recon = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT);
        if (k)
            s.field = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16_FLOAT);
        for (auto& hist : s.state)
            hist =
                f.texture(mw, mh, nullptr, (k || revision) ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
    }
    f.r.finish();
    f.uploads.clear();
    std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    const std::string oldDir = !trial.empty() ? "artifacts/ghost-study/compact-math-baseline"
                               : retirement   ? "artifacts/ghost-study/carrier-retirement-baseline"
                                              : "artifacts/ghost-study/compact-baseline";
    auto clear = f.r.pipeline(dir, "private_ghost_clear"),
         baseScatter =
             f.r.pipeline(revision ? oldDir : dir, revision ? "private_ghost_compact_wave" : "private_ghost_scatter"),
         baseAge =
             f.r.pipeline(revision ? oldDir : dir, revision ? "private_ghost_compact_2" : "private_ghost_history"),
         baseDecode =
             f.r.pipeline(revision ? oldDir : dir, retirement ? (residual ? "private_carrier_compact_fast"
                                                                          : "private_carrier_compact_fast_answer")
                                                   : revision ? "private_carrier_compact_native16"
                                                              : "private_carrier_temporal_ghost"),
         scatter = f.r.pipeline(dir, "private_ghost_coarse_1"), age = f.r.pipeline(dir, "private_ghost_coarse_2"),
         decode16 = f.r.pipeline(dir, residual ? "private_carrier_coarse" : "private_carrier_coarse_answer");
    auto dilate = f.r.pipeline(dir, radius == 2 ? "private_ghost_coarse_dilate_2" : "private_ghost_coarse_dilate_1");
    ComPtr<ID3D12QueryHeap> queries;
    D3D12_QUERY_HEAP_DESC desc {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2;
    check(f.r.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queries)));
    auto readback = f.r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency;
    check(f.r.queue->GetTimestampFrequency(&frequency));
    auto measure = [&](unsigned variant)
    {
        auto& s = states[variant];
        auto& out = outputs[variant];
        const unsigned stride = variant ? 4 : 2;
        const unsigned mw = (w + stride - 1) / stride, mh = (h + stride - 1) / stride;
        unsigned next = s.valid ? 1 - s.index : 0;
        f.r.begin();
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (s.valid)
            f.r.barrier(s.recon.Get(), SRV, UAV);
        if (s.valid || next == 1)
            f.r.barrier(s.state[next].Get(), SRV, UAV);
        DlssNr::Superior::DlssNrConstants c {};
        DlssNr::GhostRejection::Settings settings { true, .01f, .02f, 1, false, 2 };
        DlssNr::Superior::Fields::SetGhostVisibility(c, mw, mh, w, h, settings, true, s.valid, 0);

        const bool compact = variant || revision;
        if (compact && !s.valid)
            f.r.barrier(s.state[1].Get(), UAV, SRV);
        auto* previous = s.valid ? s.state[s.index].Get() : compact ? s.state[1].Get() : d.Get();
        c.Mode = 44;
        f.dispatch(clear.Get(), c, d.Get(), m.Get(), s.recon.Get(), (mw + 7) / 8, (mh + 7) / 8, nullptr, previous,
                   nullptr, nullptr, nullptr, 0, false);
        const auto uavBarrier = [&](ID3D12Resource* resource)
        {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            f.r.list->ResourceBarrier(1, &barrier);
        };
        if (variant || retirement)
            uavBarrier(s.recon.Get());
        else
        {
            f.r.barrier(s.recon.Get(), UAV, SRV);
            f.r.barrier(s.recon.Get(), SRV, UAV);
        }
        c.Mode = 45;
        f.dispatch(variant ? scatter.Get() : baseScatter.Get(), c, d.Get(), m.Get(), s.recon.Get(), (mw + 7) / 8,
                   (mh + 7) / 8, nullptr, previous, nullptr, nullptr, nullptr, 1, false,
                   compact ? s.state[next].Get() : nullptr);
        f.r.barrier(s.recon.Get(), UAV, SRV);
        if (compact)
        {
            if (variant || retirement)
                uavBarrier(s.state[next].Get());
            else
            {
                f.r.barrier(s.state[next].Get(), UAV, SRV);
                f.r.barrier(s.state[next].Get(), SRV, UAV);
            }
        }
        c.Mode = 46;
        f.dispatch(variant ? age.Get() : baseAge.Get(), c, d.Get(), m.Get(), s.state[next].Get(), (mw + 7) / 8,
                   (mh + 7) / 8, s.recon.Get(), previous, nullptr, nullptr, nullptr, 2, false);
        f.r.barrier(s.state[next].Get(), UAV, SRV);
        if (!s.valid && !compact)
            f.r.barrier(s.state[1].Get(), UAV, SRV);
        ID3D12Resource* decodeGuide = s.state[next].Get();
        if (s.field)
        {
            if (s.valid)
                f.r.barrier(s.field.Get(), SRV, UAV);
            DlssNr::Superior::Fields::SetCoarseGhostDilate(c, mw, mh, settings);
            f.dispatch(dilate.Get(), c, s.state[next].Get(), s.state[next].Get(), s.field.Get(), (mw + 7) / 8,
                       (mh + 7) / 8, nullptr, nullptr, nullptr, nullptr, nullptr, 3, false);
            f.r.barrier(s.field.Get(), UAV, SRV);
            decodeGuide = s.field.Get();
        }

        DlssNr::Superior::DlssNrConstants dc {};
        dc.Mode = 38;
        dc.Width = w;
        dc.Height = h;
        DlssNr::Superior::Fields::SetPositiveCarrierGain(dc, gMathGain);
        DlssNr::Superior::Fields::SetUnfilteredEditWeight(dc, gMathBlend);
        DlssNr::Superior::Fields::SetCarrierSaturation(dc, 1);
        DlssNr::Superior::Fields::SetCarrierNrAnswer(dc, !residual);
        DlssNr::Superior::Fields::SetCarrierDepthColour(dc, 0, 1, 0, true);
        settings.softExpansion = .5f;
        DlssNr::Superior::Fields::SetCarrierGhostRejection(dc, settings);
        DlssNr::Superior::Fields::SetGhostDepthPrecision(dc, 0);
        f.dispatch(variant ? decode16.Get() : baseDecode.Get(), dc, a.Get(), p.Get(), out.Get(), (w + 7) / 8,
                   (h + 7) / 8, d.Get(), p.Get(), r.Get(), decodeGuide, nullptr, 4, false);
        f.r.barrier(out.Get(), UAV, SRV);
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        f.r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        f.r.barrier(out.Get(), SRV, UAV);
        f.r.finish();
        f.uploads.clear();
        s.index = next;
        s.valid = true;
        UINT64* values;
        check(readback->Map(0, nullptr, reinterpret_cast<void**>(&values)));
        double ms = double(values[1] - values[0]) * 1000 / frequency;
        readback->Unmap(0, nullptr);
        return ms;
    };
    const auto name = std::filesystem::path(stem).filename().string();
    const auto prefix =
        "artifacts/ghost-study/" + name + ".coarse-r" + std::to_string(radius) + (residual ? "-edit" : "-answer");
    std::ofstream csv(prefix + "-timing.csv");
    csv << "sample,width,height,comparison,block,position,variant,ms\n";
    const unsigned blocks = revision ? 64u : 16u;
    for (unsigned candidate = 1; candidate <= 1; ++candidate)
    {
        for (unsigned warm = 0; warm < 16; ++warm)
        {
            measure(0);
            measure(candidate);
        }
        for (unsigned block = 0; block < blocks; ++block)
            for (unsigned pos = 0; pos < 4; ++pos)
            {
                unsigned variant = pos == 1 || pos == 2 ? candidate : 0;
                csv << name << ',' << w << ',' << h << ',' << candidate << ',' << block << ',' << pos << ',' << variant
                    << ',' << measure(variant) << '\n';
            }
    }
    if (revision)
    {
        const auto readSrv = [&](ID3D12Resource* resource)
        {
            f.r.begin();
            f.r.barrier(resource, SRV, UAV);
            return f.read(resource);
        };
        auto oldOutput = readSrv(outputs[0].Get()), newOutput = readSrv(outputs[1].Get());
        double sum = 0, sumSquared = 0;
        float maxRGB = 0, maxMask = 0;
        size_t changed = 0, increased = 0, decreased = 0, strongLost = 0, soft = 0;
        for (size_t i = 0; i < oldOutput.size(); ++i)
        {
            if (!std::isfinite(newOutput[i]))
                throw std::runtime_error("Nonfinite coarse output");
            const float delta = std::abs(newOutput[i] - oldOutput[i]);
            if (i % 4 == 3)
            {
                if (newOutput[i] < 0 || newOutput[i] > 1)
                    throw std::runtime_error("Invalid coarse weight");
                maxMask = std::max(maxMask, delta);
                changed += delta > 0;
                increased += newOutput[i] > oldOutput[i] + .01f;
                decreased += newOutput[i] < oldOutput[i] - .01f;
                strongLost += oldOutput[i] > .9f && newOutput[i] < .1f;
                soft += newOutput[i] > 0 && newOutput[i] < 1;
            }
            else
            {
                maxRGB = std::max(maxRGB, delta);
                sum += delta;
                sumSquared += double(delta) * delta;
            }
        }
        std::ofstream quality(prefix + "-quality.json");
        const size_t pixels = oldOutput.size() / 4;
        quality << "{\n  \"pixels\": " << pixels << ",\n  \"max_rgb_difference\": " << maxRGB
                << ",\n  \"mean_rgb_difference\": " << sum / (pixels * 3)
                << ",\n  \"rms_rgb_difference\": " << std::sqrt(sumSquared / (pixels * 3))
                << ",\n  \"max_mask_difference\": " << maxMask << ",\n  \"changed_mask_pixels\": " << changed
                << ",\n  \"increased_mask_pixels\": " << increased << ",\n  \"decreased_mask_pixels\": " << decreased
                << ",\n  \"strong_mask_lost_pixels\": " << strongLost << ",\n  \"soft_mask_pixels\": " << soft
                << ",\n  \"history_bit_exact\": false\n}\n";
        std::cout << "Coarse quality: increased " << increased << ", decreased " << decreased << ", strong loss "
                  << strongLost << " / " << pixels << " pixels\n";
    }
    std::cout << "Coarse whole-region GPU ABBA saved for " << name << '\n';
}
