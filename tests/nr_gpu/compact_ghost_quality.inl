// Production shaders, matched captures, complete visibility+decode region, FP16 colour.
void CompactGhostTiming(Fixture& f, const std::string& stem)
{
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || w != dw || w != mw || h != ah || h != dh || h != mh)
        throw std::runtime_error("Compact timing dimensions");
    std::vector<float> zero(raw.size(), 0);
    f.r.begin();
    auto r = f.texture(w, h, &raw, DXGI_FORMAT_R16G16B16A16_FLOAT),
         a = f.texture(w, h, &after, DXGI_FORMAT_R16G16B16A16_FLOAT),
         p = f.texture(w, h, &zero, DXGI_FORMAT_R16G16B16A16_FLOAT), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
         m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT),
         out = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
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
            hist = f.texture(mw, mh, nullptr, k ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
    }
    f.r.finish();
    f.uploads.clear();
    std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    auto clear = f.r.pipeline(dir, "private_ghost_clear"), baseScatter = f.r.pipeline(dir, "private_ghost_scatter"),
         baseAge = f.r.pipeline(dir, "private_ghost_history"),
         baseDecode = f.r.pipeline(dir, "private_carrier_temporal_ghost"),
         scatter = f.r.pipeline(dir, "private_ghost_compact_1"), age = f.r.pipeline(dir, "private_ghost_compact_2"),
         decode16 = f.r.pipeline(dir, "private_carrier_compact_native16");
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

        if (variant && !s.valid)
            f.r.barrier(s.state[1].Get(), UAV, SRV);
        auto* previous = s.valid ? s.state[s.index].Get() : variant ? s.state[1].Get() : d.Get();
        c.Mode = 44;
        f.dispatch(clear.Get(), c, d.Get(), m.Get(), s.recon.Get(), (mw + 7) / 8, (mh + 7) / 8, nullptr, previous,
                   nullptr, nullptr, nullptr, 0, false);
        f.r.barrier(s.recon.Get(), UAV, SRV);
        f.r.barrier(s.recon.Get(), SRV, UAV);
        c.Mode = 45;
        f.dispatch(variant ? scatter.Get() : baseScatter.Get(), c, d.Get(), m.Get(), s.recon.Get(), (mw + 7) / 8,
                   (mh + 7) / 8, nullptr, previous, nullptr, nullptr, nullptr, 1, false,
                   variant ? s.state[next].Get() : nullptr);
        f.r.barrier(s.recon.Get(), UAV, SRV);
        if (variant)
        {
            f.r.barrier(s.state[next].Get(), UAV, SRV);
            f.r.barrier(s.state[next].Get(), SRV, UAV);
        }
        c.Mode = 46;
        f.dispatch(variant ? age.Get() : baseAge.Get(), c, d.Get(), m.Get(), s.state[next].Get(), (mw + 7) / 8,
                   (mh + 7) / 8, s.recon.Get(), previous, nullptr, nullptr, nullptr, 2, false);
        f.r.barrier(s.state[next].Get(), UAV, SRV);
        if (!s.valid && !variant)
            f.r.barrier(s.state[1].Get(), UAV, SRV);
        ID3D12Resource* decodeGuide = s.state[next].Get();
        DlssNr::Superior::DlssNrConstants dc {};
        dc.Mode = 38;
        dc.Width = w;
        dc.Height = h;
        DlssNr::Superior::Fields::SetPositiveCarrierGain(dc, 1);
        DlssNr::Superior::Fields::SetCarrierSaturation(dc, 1);
        DlssNr::Superior::Fields::SetCarrierNrAnswer(dc, true);
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
    std::ofstream csv("artifacts/ghost-study/" + name + ".compact-final-timing.csv");
    csv << "sample,width,height,comparison,block,position,variant,ms\n";
    for (unsigned candidate = 1; candidate <= 1; ++candidate)
    {
        for (unsigned warm = 0; warm < 16; ++warm)
        {
            measure(0);
            measure(candidate);
        }
        for (unsigned block = 0; block < 16; ++block)
            for (unsigned pos = 0; pos < 4; ++pos)
            {
                unsigned variant = pos == 1 || pos == 2 ? candidate : 0;
                csv << name << ',' << w << ',' << h << ',' << candidate << ',' << block << ',' << pos << ',' << variant
                    << ',' << measure(variant) << '\n';
            }
    }
    std::cout << "Compact native16 GPU ABBA saved for " << name << '\n';
}

// Independent scalar decode reference: supplied integer timers, not the history implementation.
void CompactGhostQuality(Fixture& f)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options {};
    bool native = SUCCEEDED(f.r.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options))) &&
                  options.Native16BitShaderOpsSupported;
    unsigned checks = 0;
    for (bool useNative : { true })
    {
        if (useNative && !native)
        {
            std::cout << "Native FP16 mask quality skipped: unavailable device\n";
            continue;
        }
        auto decode = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_compact_native16");
        for (float expansion : { 0.f, .5f, 1.f })
            for (unsigned fade : { 0u, 3u })
                for (bool full : { false, true })
                    for (bool reversed : { false, true })
                        for (unsigned scenario = 0; scenario < 3; ++scenario)
                        {
                            constexpr UINT w = 17, h = 9, mw = 9, mh = 5;
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
                                 e = f.texture(w, h, &encoded), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
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
                            DlssNr::Superior::Fields::SetGhostDepthPrecision(c, 0);
                            DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
                            f.dispatch(decode.Get(), c, e.Get(), p.Get(), out.Get(), 3, 2, d.Get(), p.Get(), a.Get(),
                                       s.Get());
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
                                            std::abs(got[i + ch] - expected) > (useNative ? 0.00025 : 0.000003))
                                            throw std::runtime_error(
                                                "Compact decode independent RGB reference failed native=" +
                                                std::to_string(useNative) + " expansion=" + std::to_string(expansion) +
                                                " fade=" + std::to_string(fade) + " full=" + std::to_string(full) +
                                                " scenario=" + std::to_string(scenario) + " xy=" + std::to_string(x) +
                                                "," + std::to_string(y) + " got=" + std::to_string(got[i + ch]) +
                                                " expected=" + std::to_string(expected) + " alpha=" +
                                                std::to_string(got[i + 3]) + " mask=" + std::to_string(mask));
                                    }
                                    if (std::abs(got[i + 3] - mask * .75) > (useNative ? .0015 : .000003))
                                        throw std::runtime_error("Compact tent/depth/fade mask reference failed");
                                }
                            ++checks;
                        }
    }
    std::cout << "Compact mask PASS: " << checks
              << " independent expansion/zero-age/occluder/native16/transport/fade cases\n";
}
