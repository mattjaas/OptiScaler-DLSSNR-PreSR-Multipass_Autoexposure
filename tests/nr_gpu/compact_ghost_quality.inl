// Production shaders, matched captures, complete visibility+decode region, FP16 colour.
void CompactGhostTiming(Fixture& f, const std::string& stem, bool revision = false, bool residual = false,
                        bool retirement = false, bool fast = false, bool math = false, float gain = 1.f,
                        float globalFade = 0.f)
{
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || w != dw || w != mw || h != ah || h != dh || h != mh)
        throw std::runtime_error("Compact timing dimensions");
    for (size_t i = 0; i < after.size(); i += 4)
        for (unsigned ch = 0; ch < 3; ++ch)
            if (after[i + ch] > 0)
                after[i + ch] *= gain;
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
        ComPtr<ID3D12Resource> recon, state[2];
        unsigned index = 0;
        bool valid = false;
    };
    State states[2];
    for (unsigned k = 0; k < 2; ++k)
    {
        auto& s = states[k];
        s.recon = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT);
        for (auto& hist : s.state)
            hist =
                f.texture(mw, mh, nullptr, (k || revision) ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
    }
    f.r.finish();
    f.uploads.clear();
    std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    const std::string oldDir = math         ? "artifacts/ghost-study/compact-math-baseline"
                               : fast       ? "artifacts/ghost-study/fast-blend-baseline"
                               : retirement ? "artifacts/ghost-study/carrier-retirement-baseline"
                                            : "artifacts/ghost-study/compact-baseline";
    auto clear = f.r.pipeline(dir, "private_ghost_clear"),
         baseScatter = f.r.pipeline(revision ? oldDir : dir, math       ? "private_ghost_compact_wave"
                                                             : revision ? "private_ghost_compact_1"
                                                                        : "private_ghost_scatter"),
         baseAge =
             f.r.pipeline(revision ? oldDir : dir, revision ? "private_ghost_compact_2" : "private_ghost_history"),
         baseDecode = f.r.pipeline(
             revision ? oldDir : dir,
             retirement
                 ? (residual ? (math ? "private_carrier_compact_fast" : "private_carrier_compact_simple")
                             : (math ? "private_carrier_compact_fast_answer" : "private_carrier_compact_simple_answer"))
             : revision ? "private_carrier_compact_native16"
                        : "private_carrier_temporal_ghost"),
         scatter = f.r.pipeline(dir, fast ? "private_ghost_compact_wave" : "private_ghost_compact_1"),
         age = f.r.pipeline(dir, "private_ghost_compact_2"),
         decode16 = f.r.pipeline(
             dir,
             fast ? (residual ? (globalFade > 0 ? "private_carrier_compact_fast_fade" : "private_carrier_compact_fast")
                              : "private_carrier_compact_fast_answer")
                  : (residual ? "private_carrier_compact_simple" : "private_carrier_compact_simple_answer"));
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
        DlssNr::Superior::DlssNrConstants dc {};
        dc.Mode = 38;
        dc.Width = w;
        dc.Height = h;
        DlssNr::Superior::Fields::SetPositiveCarrierGain(dc, gain);
        DlssNr::Superior::Fields::SetUnfilteredEditWeight(dc, globalFade);
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
    std::ofstream csv(
        "artifacts/ghost-study/" + name +
        (math ? ".compact-math-g" + std::to_string(int(gain)) + "-b" + std::to_string(int(globalFade * 100)) +
                    (residual ? "-edit-timing.csv" : "-answer-timing.csv")
         : fast       ? (residual ? ".compact-fast-edit-timing.csv" : ".compact-fast-answer-timing.csv")
         : retirement ? (residual ? ".carrier-retirement-edit-timing.csv" : ".carrier-retirement-answer-timing.csv")
         : revision   ? (residual ? ".compact-revision-edit-timing.csv" : ".compact-revision-timing.csv")
                      : (residual ? ".compact-final-edit-timing.csv" : ".compact-final-timing.csv")));
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
        auto oldState = readSrv(states[0].state[states[0].index].Get()),
             newState = readSrv(states[1].state[states[1].index].Get());
        if (oldState.size() != newState.size() ||
            std::memcmp(oldState.data(), newState.data(), oldState.size() * sizeof(float)))
            throw std::runtime_error("Compact revision changed history words");
        auto oldOutput = readSrv(outputs[0].Get()), newOutput = readSrv(outputs[1].Get());
        if (oldOutput.size() != newOutput.size())
            throw std::runtime_error("Compact comparison output size");
        if (!fast)
        {
            if (std::memcmp(oldOutput.data(), newOutput.data(), oldOutput.size() * sizeof(float)))
                throw std::runtime_error("Compact revision changed decoded FP16 pixels");
            std::cout << "Frozen reference history words and FP16 decode pixels: bit-exact PASS\n";
        }
        else
        {
            double sumRGB = 0, sumSquaredRGB = 0;
            float maxRGB = 0, maxMask = 0;
            size_t changedRGB = 0, changedMask = 0;
            for (size_t i = 0; i < oldOutput.size(); ++i)
            {
                const float delta = std::abs(oldOutput[i] - newOutput[i]);
                if (!std::isfinite(newOutput[i]))
                    throw std::runtime_error("Fast blend nonfinite output");
                if (i % 4 == 3)
                {
                    maxMask = std::max(maxMask, delta);
                    changedMask += delta > 0;
                    if (delta > .002f)
                        throw std::runtime_error("Fast blend mask error bound");
                }
                else
                {
                    maxRGB = std::max(maxRGB, delta);
                    sumRGB += delta;
                    sumSquaredRGB += double(delta) * delta;
                    changedRGB += delta > 0;
                    // Quantization and a small fraction of the actual raw/filtered contrast.
                    float filtered = after[i] - (residual ? .5f : 0.f);
                    if (filtered > 0)
                        filtered /= gain;
                    const float bound = .003f * std::abs(raw[i] - filtered) +
                                        .002f * std::max(std::abs(oldOutput[i]), std::abs(newOutput[i])) + 1e-6f;
                    if (delta > bound)
                        throw std::runtime_error("Fast blend RGB error bound");
                }
            }
            const double channels = double(oldOutput.size() / 4) * 3;
            const auto qualityPath =
                "artifacts/ghost-study/" + name +
                (math ? ".compact-math-g" + std::to_string(int(gain)) + "-b" + std::to_string(int(globalFade * 100)) +
                            (residual ? "-edit-quality.json" : "-answer-quality.json")
                      : (residual ? ".compact-fast-edit-quality.json" : ".compact-fast-answer-quality.json"));
            std::ofstream quality(qualityPath);
            quality << "{\n  \"pixels\": " << oldOutput.size() / 4 << ",\n  \"max_rgb_error\": " << maxRGB
                    << ",\n  \"mean_rgb_error\": " << sumRGB / channels
                    << ",\n  \"rms_rgb_error\": " << std::sqrt(sumSquaredRGB / channels)
                    << ",\n  \"max_mask_error\": " << maxMask << ",\n  \"changed_rgb_channels\": " << changedRGB
                    << ",\n  \"changed_mask_pixels\": " << changedMask << ",\n  \"history_bit_exact\": true\n}\n";
            std::cout << "Fast blend: history bit-exact PASS; max RGB " << maxRGB << ", max mask " << maxMask
                      << ", changed mask " << changedMask << " / " << oldOutput.size() / 4 << " pixels\n";
        }
    }
    std::cout << "Compact GPU ABBA saved for " << name << '\n';
}

// Independent scalar decode reference: supplied integer timers, not the history implementation.
void CompactGhostQuality(Fixture& f, bool fast = false)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options {};
    bool native = SUCCEEDED(f.r.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options))) &&
                  options.Native16BitShaderOpsSupported;
    unsigned checks = 0;
    for (bool useNative : { true })
    {
        if (useNative && !native && !fast)
        {
            std::cout << "Native FP16 mask quality skipped: unavailable device\n";
            continue;
        }
        for (bool staleFields : { false, true })
        {
            auto decode = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile",
                                       fast ? "private_carrier_compact_fast_fade" : "private_carrier_compact_simple");
            auto answerDecode =
                f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile",
                             fast ? "private_carrier_compact_fast_answer" : "private_carrier_compact_simple_answer");
            for (float expansion : { 0.f, .5f, 1.f })
                for (unsigned fade : { 0u, 3u })
                    for (bool full : { false, true })
                        for (bool reversed : { false, true })
                            for (unsigned scenario = 0; scenario < 3; ++scenario)
                                for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 17, 9 }, { 1281, 7 } })
                                {
                                    const UINT w = size.first, h = size.second, mw = (w + 1) / 2, mh = (h + 1) / 2;
                                    std::vector<float> proxy(w * h * 4, .2f), answer(proxy), encoded(proxy),
                                        depth(proxy.size(), 0), state(mw * mh * 4, 0);
                                    std::vector<uint32_t> depthBits(mw * mh), ages(mw * mh);
                                    const float raw[3] = { .01f, -.02f, .03f }, filtered[3] = { .08f, -.04f, .12f };
                                    for (UINT y = 0; y < h; ++y)
                                        for (UINT x = 0; x < w; ++x)
                                        {
                                            size_t i = (y * w + x) * 4;
                                            depth[i] = reversed ? .01f : 1 - .01f;
                                            for (unsigned ch = 0; ch < 3; ++ch)
                                            {
                                                answer[i + ch] = proxy[i + ch] + raw[ch];
                                                encoded[i + ch] = (full ? proxy[i + ch] : .5f) +
                                                                  filtered[ch] * (filtered[ch] > 0 ? 6.f : 1.f);
                                            }
                                        }
                                    for (UINT y = 0; y < mh; ++y)
                                        for (UINT x = 0; x < mw; ++x)
                                        {
                                            size_t i = y * mw + x;
                                            float z = scenario == 2 ? .1f : .01f;
                                            uint32_t bits;
                                            std::memcpy(&bits, &z, 4);
                                            bits &= 0xffffff80u;
                                            depthBits[i] = bits;
                                            ages[i] = scenario == 0 ? 0u : x % 4;
                                            uint32_t packed = bits | ages[i];
                                            std::memcpy(&state[i * 4], &packed, 4);
                                            uint32_t mv = 0;
                                            std::memcpy(&state[i * 4 + 1], &mv, 4);
                                        }
                                    f.r.begin();
                                    auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer),
                                         e = f.texture(w, h, &encoded),
                                         d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                                         s = f.texture(mw, mh, &state, DXGI_FORMAT_R32G32_UINT), out = f.texture(w, h);
                                    DlssNr::Superior::DlssNrConstants c {};
                                    c.Mode = 38;
                                    c.Width = w;
                                    c.Height = h;
                                    DlssNr::Superior::Fields::SetPositiveCarrierGain(c, 6);
                                    DlssNr::Superior::Fields::SetCarrierSaturation(c, 1);
                                    DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
                                    DlssNr::Superior::Fields::SetCarrierDepthColour(c, 0, 1, 0, reversed);
                                    DlssNr::GhostRejection::Settings settings { true, .01f, .02f, .75f, false, 3 };
                                    settings.historyFade = fade;
                                    settings.softExpansion = expansion;
                                    DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
                                    if (staleFields)
                                    {
                                        c.MaxRatio = 64;
                                        c.ResidualScale = 16;
                                        c.ResidualBlend = .1f;
                                        c.DirectResolveUpscaler = 7;
                                    }
                                    DlssNr::Superior::Fields::SetGhostDepthPrecision(c, 0);
                                    DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
                                    f.dispatch(full ? answerDecode.Get() : decode.Get(), c, e.Get(), p.Get(), out.Get(),
                                               (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(), a.Get(), s.Get());
                                    auto got = f.read(out.Get());
                                    for (UINT y = 0; y < h; ++y)
                                        for (UINT x = 0; x < w; ++x)
                                        {
                                            double px = (x + .5) * mw / w - .5, py = (y + .5) * mh / h - .5;
                                            int ix = int(std::floor(px)), iy = int(std::floor(py));
                                            double fx = px - ix, fy = py - iy;
                                            double timer = 0, broadened = 0;
                                            if (scenario == 1)
                                                for (int yy = 0; yy < 2; ++yy)
                                                    for (int xx = 0; xx < 2; ++xx)
                                                    {
                                                        unsigned age = ages[std::clamp(iy + yy, 0, int(mh) - 1) * mw +
                                                                            std::clamp(ix + xx, 0, int(mw) - 1)];
                                                        double tx = xx ? fx : 1 - fx, ty = yy ? fy : 1 - fy;
                                                        timer += age * tx * ty;
                                                        broadened = std::max(broadened, age * std::min(tx, ty));
                                                    }
                                            double t = std::max(timer, timer + (broadened - timer) * expansion);
                                            double mask = t <= 0 ? 0 : .75 * (fade ? std::min(t / fade, 1.) : 1.);
                                            size_t i = (y * w + x) * 4;
                                            for (unsigned ch = 0; ch < 3; ++ch)
                                            {
                                                double expected =
                                                    .75 * ((1 - mask) * filtered[ch] + mask * raw[ch]) + .25 * raw[ch];
                                                if (!std::isfinite(got[i + ch]) ||
                                                    std::abs(got[i + ch] - expected) > (fast ? 0.000003 : 0.00025))
                                                    throw std::runtime_error(
                                                        "Compact decode independent RGB reference failed native=" +
                                                        std::to_string(useNative) + " expansion=" +
                                                        std::to_string(expansion) + " fade=" + std::to_string(fade) +
                                                        " full=" + std::to_string(full) + " scenario=" +
                                                        std::to_string(scenario) + " xy=" + std::to_string(x) + "," +
                                                        std::to_string(y) + " got=" + std::to_string(got[i + ch]) +
                                                        " expected=" + std::to_string(expected) + " alpha=" +
                                                        std::to_string(got[i + 3]) + " mask=" + std::to_string(mask));
                                            }
                                            if (std::abs(got[i + 3] - mask * .75) > (fast ? .000003 : .0015))
                                                throw std::runtime_error(
                                                    "Compact tent/depth/fade mask reference failed");
                                        }
                                    ++checks;
                                }
        }
    }
    std::cout << (fast ? "Fast compact mask PASS: " : "Compact mask PASS: ") << checks
              << " independent expansion/zero-age/occluder/native16/transport/fade cases\n";
}

// The unchanged ordinary carrier has its own independent scalar suite.
void CompactTransportQuality(Fixture& f, bool fast = false)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options {};
    if (FAILED(f.r.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options))) ||
        !options.Native16BitShaderOpsSupported)
        return;
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    auto simple = f.r.pipeline(dir, fast ? "private_carrier_compact_fast_fade" : "private_carrier_compact_simple"),
         answerSimple =
             f.r.pipeline(dir, fast ? "private_carrier_compact_fast_answer" : "private_carrier_compact_simple_answer");
    unsigned checks = 0;
    for (bool full : { false, true })
        for (bool reversed : { false, true })
            for (float gain : { .5f, 6.f })
                for (float saturation : { 1.f, 4.f })
                    for (float colour : { 0.f, .15f })
                        for (bool brightness : { false, true })
                        {
                            constexpr UINT w = 17, h = 9, mw = 9, mh = 5;
                            std::vector<float> proxy(w * h * 4, .25f), raw(proxy), filteredAnswer(proxy),
                                depth(proxy.size(), 0), state(mw * mh * 4, 0);
                            for (size_t i = 0; i < proxy.size(); i += 4)
                            {
                                depth[i] = reversed ? .25f : .75f;
                                for (unsigned ch = 0; ch < 3; ++ch)
                                {
                                    raw[i + ch] = proxy[i + ch] + (ch == 0 ? .02f : ch == 1 ? -.015f : .03f);
                                    filteredAnswer[i + ch] = proxy[i + ch] + (ch == 0 ? .08f : ch == 1 ? -.04f : .1f);
                                }
                            }
                            float nearZ = .25f;
                            uint32_t bits;
                            std::memcpy(&bits, &nearZ, 4);
                            bits = (bits & 0xffffff80u) | 16u;
                            for (size_t i = 0; i < state.size(); i += 4)
                                std::memcpy(&state[i], &bits, 4);
                            f.r.begin();
                            auto p = f.texture(w, h, &proxy), r = f.texture(w, h, &raw),
                                 a = f.texture(w, h, &filteredAnswer),
                                 d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                                 s = f.texture(mw, mh, &state, DXGI_FORMAT_R32G32_UINT), encoded = f.texture(w, h),
                                 reference = f.texture(w, h), out = f.texture(w, h);
                            DlssNr::Superior::DlssNrConstants c {};
                            c.Mode = 37;
                            c.Width = w;
                            c.Height = h;
                            DlssNr::Superior::Fields::SetPositiveCarrierGain(c, gain);
                            DlssNr::Superior::Fields::SetCarrierSaturation(c, saturation);
                            DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
                            DlssNr::Superior::Fields::SetCarrierDepthColour(c, colour, .7f, 1, reversed);
                            DlssNr::DepthTransport::Settings transport {};
                            transport.weightedGain = brightness;
                            transport.additive = brightness;
                            transport.bias = .05f;
                            DlssNr::Superior::Fields::SetCarrierDepthBrightness(c, transport);
                            f.dispatch(f.carrier.Get(), c, p.Get(), a.Get(), encoded.Get(), 3, 2, d.Get());
                            f.r.barrier(encoded.Get(), UAV, SRV);
                            c.Mode = 38;
                            f.dispatch(f.carrier.Get(), c, encoded.Get(), p.Get(), reference.Get(), 3, 2, d.Get());
                            auto ordinary = f.read(reference.Get());
                            f.r.begin();
                            DlssNr::GhostRejection::Settings settings { true, .01f, .02f, .5f, false, 3 };
                            DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
                            DlssNr::Superior::Fields::SetGhostDepthPrecision(c, 0);
                            DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
                            f.dispatch(full ? answerSimple.Get() : simple.Get(), c, encoded.Get(), p.Get(), out.Get(),
                                       3, 2, d.Get(), p.Get(), r.Get(), s.Get());
                            auto got = f.read(out.Get());
                            for (size_t i = 0; i < got.size(); i += 4)
                            {
                                for (unsigned ch = 0; ch < 3; ++ch)
                                    Near(got[i + ch], ordinary[i + ch] * .375f + (raw[i + ch] - proxy[i + ch]) * .625f,
                                         "compact combined transport and raw blend");
                                Near(got[i + 3], .375f, "compact combined transport alpha");
                            }
                            ++checks;
                        }
    std::cout << (fast ? "Fast compact combined transport PASS: " : "Compact combined transport PASS: ") << checks
              << " gain/answer/Z cases with obsolete transform fields ignored\n";
}
