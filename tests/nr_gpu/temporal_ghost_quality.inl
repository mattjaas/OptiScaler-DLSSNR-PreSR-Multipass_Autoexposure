// Known visibility sequence; independent expected timers, not a previous implementation.
struct TemporalGhostFixture
{
    Fixture& f;
    ComPtr<ID3D12PipelineState> clear, scatter, age;
    ComPtr<ID3D12Resource> reconstructed, history[2];
    unsigned index = 0;
    UINT w = 128, h = 64, mw = 64, mh = 32;
    bool compact = false;
    bool valid = false, wasRead[2] = { false, false };
    TemporalGhostFixture(Fixture& fixture, UINT width = 128, UINT height = 64, bool packed = false, bool fast = false,
                         unsigned step = 2)
        : f(fixture), compact(packed), w(width), h(height), mw((width + step - 1) / step),
          mh((height + step - 1) / step)
    {
        clear = f.visibilityPipeline("private_ghost_clear");
        scatter = f.visibilityPipeline(step == 4 ? "private_ghost_coarse_1"
                                       : fast    ? "private_ghost_compact_wave"
                                       : compact ? "private_ghost_compact_1"
                                                 : "private_ghost_scatter");
        age = f.visibilityPipeline(step == 4 ? "private_ghost_coarse_2"
                                   : compact ? "private_ghost_compact_2"
                                             : "private_ghost_history");
        f.r.begin();
        reconstructed = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT);
        history[0] = f.texture(mw, mh, nullptr, compact ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
        history[1] = f.texture(mw, mh, nullptr, compact ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT);
        f.r.finish();
    }
    std::vector<float> Frame(int left, int objectWidth, float nearObject, float bgMv, float objectMv,
                             bool reset = false, bool reversed = true, bool invalid = false, unsigned bits = 0)
    {
        std::vector<float> z(w * h * 4, .0f), mv(z);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                const size_t i = (y * w + x) * 4;
                bool object = int(x) >= left && int(x) < left + objectWidth && y >= 16 && y < 48;
                const float nearZ = object ? nearObject : .01f;
                float zValue = reversed ? nearZ : 1 - nearZ;
                if (bits)
                    zValue = std::round(zValue * ((1u << bits) - 1)) / float((1u << bits) - 1);
                z[i] = invalid ? NAN : zValue;
                mv[i] = invalid ? INFINITY : object ? objectMv : bgMv;
            }
        return FrameGuides(z, mv, reset, reversed, bits);
    }
    std::vector<float> FrameGuides(const std::vector<float>& z, const std::vector<float>& mv, bool reset = false,
                                   bool reversed = true, unsigned bits = 0, unsigned frames = 4, unsigned fade = 2,
                                   float threshold = .05f, float gate = 2.f)
    {
        if (z.size() != size_t(w) * h * 4 || mv.size() != z.size())
            throw std::runtime_error("Visibility guide dimensions");
        f.r.begin();
        auto depth = f.texture(w, h, &z, DXGI_FORMAT_R32_FLOAT),
             motion = f.texture(w, h, &mv, DXGI_FORMAT_R32G32_FLOAT);
        DlssNr::Superior::DlssNrConstants c {};
        c.Width = mw;
        c.Height = mh;
        c.GuideWidth = w;
        c.GuideHeight = h;
        c.WhitePoint = float(frames);
        c.TransferStrength = float(fade);
        c.ColourStrength = threshold;
        c.MaxRatio = gate;
        c.Passthrough = reversed;
        c.MvScaleX = bits ? 1.f / float((1u << bits) - 1) : 0;
        c.DebugView = valid && !reset;
        if (compact && !c.DebugView && !wasRead[1])
        {
            f.r.barrier(history[1].Get(), UAV, SRV);
            wasRead[1] = true;
        }
        auto* previous = c.DebugView ? history[index].Get() : compact ? history[1].Get() : depth.Get();
        const unsigned next = c.DebugView ? 1 - index : 0;
        if (wasRead[next])
        {
            f.r.barrier(history[next].Get(), SRV, UAV);
        }
        if (valid)
            f.r.barrier(reconstructed.Get(), SRV, UAV);
        c.Mode = 44;
        f.dispatch(clear.Get(), c, depth.Get(), motion.Get(), reconstructed.Get(), (mw + 7) / 8, (mh + 7) / 8);
        f.r.barrier(reconstructed.Get(), UAV, SRV);
        f.r.barrier(reconstructed.Get(), SRV, UAV);
        c.Mode = 45;
        f.dispatch(scatter.Get(), c, depth.Get(), motion.Get(), reconstructed.Get(), (mw + 7) / 8, (mh + 7) / 8,
                   nullptr, nullptr, nullptr, nullptr, nullptr, 0, true, compact ? history[next].Get() : nullptr);
        if (compact)
        {
            D3D12_RESOURCE_BARRIER b {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = history[next].Get();
            f.r.list->ResourceBarrier(1, &b);
        }
        f.r.barrier(reconstructed.Get(), UAV, SRV);
        c.Mode = 46;
        f.dispatch(age.Get(), c, depth.Get(), motion.Get(), history[next].Get(), (mw + 7) / 8, (mh + 7) / 8,
                   reconstructed.Get(), previous);
        auto values = f.read(history[next].Get());
        if (compact)
            for (size_t i = 0; i < values.size(); i += 4)
            {
                uint32_t z, m;
                std::memcpy(&z, &values[i], 4);
                std::memcpy(&m, &values[i + 1], 4);
                uint32_t depthBits = z & 0xffffff80u;
                std::memcpy(&values[i], &depthBits, 4);
                values[i + 1] = float(z & 127u);
                values[i + 2] = TestHalf(m & 65535u);
                values[i + 3] = TestHalf(m >> 16);
            }
        f.r.begin();
        f.r.barrier(history[next].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, SRV);
        f.r.finish();
        index = next;
        valid = true;
        wasRead[next] = true;
        for (float v : values)
            if (!std::isfinite(v))
                throw std::runtime_error("Visibility history must stay finite");
        return values;
    }
};
void TemporalGhostQuality(Fixture& f)
{
    unsigned checks = 0;
    for (unsigned variant : { 0u, 1u, 2u })
        for (bool reversed : { false, true })
        {
            TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2);
            auto first = t.Frame(40, 4, .0102f, 0, -4, false, reversed);
            for (size_t i = 1; i < first.size(); i += 4)
                if (first[i] != 0)
                    throw std::runtime_error("First visibility frame must warm up without a mask");
            auto opened = t.Frame(44, 4, .0102f, 0, -4, false, reversed);
            if (opened[(12 * t.mw + 20) * 4 + 1] != 4)
                throw std::runtime_error("Contact-depth MV disocclusion did not start N-frame window");
            if (opened[(12 * t.mw + 22) * 4 + 1] != 0)
                throw std::runtime_error("Visible occluding foot must not inherit background mask");
            for (int step = 1; step <= 4; ++step)
            {
                auto tracked = t.Frame(44 - step * 2, 4, .0102f, 2, 2, false, reversed);
                const int coordinate = 20 - step;
                const float expected = float(4 - step);
                if (tracked[(12 * t.mw + coordinate) * 4 + 1] != expected)
                    throw std::runtime_error("Visibility timer must follow background MV and expire exactly, step=" +
                                             std::to_string(step));
                ++checks;
            }
            auto reset = t.Frame(36, 4, .0102f, 0, 0, true, reversed);
            for (size_t i = 1; i < reset.size(); i += 4)
                if (reset[i] != 0)
                    throw std::runtime_error("History reset left old visibility timers");
            auto bad = t.Frame(36, 4, .0102f, 0, 0, false, reversed, true);
            for (size_t i = 1; i < bad.size(); i += 4)
                if (bad[i] != 0)
                    throw std::runtime_error("Invalid guides created a visibility mask");
            checks += 5;
        }
    for (unsigned variant : { 0u, 1u, 2u })
    {
        TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2);
        t.Frame(0, 0, .03f, 0, 0);
        auto camera = t.Frame(0, 0, .03f, 12, 12);
        for (size_t i = 1; i < camera.size(); i += 4)
            if (camera[i] != 0)
                throw std::runtime_error("Uniform camera acceleration is not a disocclusion");
        ++checks;
    }
    for (unsigned variant : { 0u, 1u, 2u })
    {
        TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2);
        t.Frame(40, 12, .03f, 0, 0);
        auto exposed = t.Frame(44, 12, .03f, 0, -4);
        if (exposed[(12 * t.mw + 20) * 4 + 1] != 4)
            throw std::runtime_error("Reconstructed-depth disocclusion missed moving foreground");
        ++checks;
    }
    for (unsigned variant : { 0u, 1u, 2u })
        for (unsigned bits : { 16u, 24u })
            for (bool reversed : { false, true })
            {
                TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2);
                t.Frame(40, 4, .0102f, 0, -4, false, reversed, false, bits);
                auto opened = t.Frame(44, 4, .0102f, 0, -4, false, reversed, false, bits);
                if (opened[(12 * t.mw + 20) * 4 + 1] != 4)
                    throw std::runtime_error("Quantized depth contact mask failed");
                ++checks;
            }
    for (unsigned variant : { 0u, 1u, 2u })
        for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 17, 9 }, { 127, 63 } })
        {
            TemporalGhostFixture t(f, size.first, size.second, variant != 0, variant == 2);
            t.Frame(0, 0, .03f, 0, 0);
            auto frame = t.Frame(0, 0, .03f, 0, 0);
            for (size_t i = 1; i < frame.size(); i += 4)
                if (frame[i] != 0)
                    throw std::runtime_error("Odd/edge visibility must remain unmasked");
            ++checks;
        }
    // Independent bilinear timer reference: zero-age neighbours must contribute to the
    // denominator; zero fade must still preserve all never-disoccluded pixels.
    auto decode = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_temporal_ghost");
    for (float gain : { .5f, 6.f })
        for (bool full : { false, true })
            for (unsigned fade : { 0u, 2u })
                for (unsigned scenario = 0; scenario < 3; ++scenario)
                    for (float strength : { .5f, 1.f })
                    {
                        constexpr UINT w = 16, h = 8, mw = 8, mh = 4;
                        std::vector<float> proxy(w * h * 4, .2f), answer(proxy), encoded(proxy), depth(proxy.size(), 0),
                            state(mw * mh * 4, 0);
                        const float raw[3] = { .01f, -.02f, .03f }, filtered[3] = { .08f, -.04f, .12f };
                        for (UINT y = 0; y < h; ++y)
                            for (UINT x = 0; x < w; ++x)
                            {
                                const size_t i = (y * w + x) * 4;
                                depth[i] = .01f;
                                for (unsigned ch = 0; ch < 3; ++ch)
                                {
                                    answer[i + ch] = proxy[i + ch] + raw[ch];
                                    encoded[i + ch] =
                                        (full ? proxy[i + ch] : .5f) + filtered[ch] * (filtered[ch] > 0 ? gain : 1.f);
                                }
                            }
                        for (UINT y = 0; y < mh; ++y)
                            for (UINT x = 0; x < mw; ++x)
                            {
                                const size_t i = (y * mw + x) * 4;
                                state[i] = scenario == 2 ? .1f : .01f; // different current surface: never transfer mask
                                state[i + 1] = scenario == 0 ? 0.f : float(x % 4);
                            }
                        f.r.begin();
                        auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer), e = f.texture(w, h, &encoded),
                             d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT), s = f.texture(mw, mh, &state),
                             out = f.texture(w, h);
                        DlssNr::Superior::DlssNrConstants c {};
                        c.Mode = 38;
                        c.Width = w;
                        c.Height = h;
                        DlssNr::Superior::Fields::SetPositiveCarrierGain(c, gain);
                        DlssNr::Superior::Fields::SetCarrierSaturation(c, 1);
                        DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
                        DlssNr::Superior::Fields::SetCarrierDepthColour(c, 0, 1, 0, true);
                        DlssNr::GhostRejection::Settings settings { true, .01f, .02f, strength, false, 2 };
                        settings.historyFade = fade;
                        DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
                        DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, .25f);
                        f.dispatch(decode.Get(), c, e.Get(), p.Get(), out.Get(), 2, 1, d.Get(), p.Get(), a.Get(),
                                   s.Get());
                        auto got = f.read(out.Get());
                        for (UINT y = 0; y < h; ++y)
                            for (UINT x = 0; x < w; ++x)
                            {
                                const double sx = (x + .5) * .5 - .5, sy = (y + .5) * .5 - .5;
                                const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                                const double fx = sx - ix, fy = sy - iy;
                                double timer = 0;
                                if (scenario == 1)
                                    for (int yy = 0; yy < 2; ++yy)
                                        for (int xx = 0; xx < 2; ++xx)
                                            timer += (std::clamp(ix + xx, 0, int(mw) - 1) % 4) * (xx ? fx : 1 - fx) *
                                                     (yy ? fy : 1 - fy);
                                const double mask = timer <= 0 ? 0 : strength * (fade ? std::min(1., timer / fade) : 1);
                                const size_t i = (y * w + x) * 4;
                                for (unsigned ch = 0; ch < 3; ++ch)
                                    Near(got[i + ch],
                                         float(.75 * ((1 - mask) * filtered[ch] + mask * raw[ch]) + .25 * raw[ch]),
                                         "temporal decode raw blend");
                                Near(got[i + 3], float(mask * .75), "temporal decode mask and global fade");
                            }
                        ++checks;
                    }
    std::cout << "Temporal visibility PASS: " << checks
              << " warmup/contact/occluder/MV-follow/expiry/reset/invalid/camera/depth checks\n";
}

// Whole changed GPU region, including transitions and all three visibility stages.
// Repeated captured guides exercise shader cost, not the quality of a temporal sequence.
void TemporalGhostTiming(Fixture& f, const std::string& stem)
{
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || w != dw || w != mw || h != ah || h != dh || h != mh)
        throw std::runtime_error("Unmatched temporal timing planes");
    std::vector<float> zero(raw.size(), 0);
    f.r.begin();
    auto r = f.texture(w, h, &raw), a = f.texture(w, h, &after), p = f.texture(w, h, &zero),
         d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT), m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT),
         out = f.texture(w, h);
    mw = (w + 1) / 2;
    mh = (h + 1) / 2;
    auto recon = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT);
    ComPtr<ID3D12Resource> states[2] = { f.texture(mw, mh), f.texture(mw, mh) };
    auto clear = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_ghost_clear"),
         scatter = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_ghost_scatter"),
         age = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_ghost_history"),
         decode = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_temporal_ghost");
    DlssNr::Superior::DlssNrConstants c {};
    c.Width = mw;
    c.Height = mh;
    c.GuideWidth = w;
    c.GuideHeight = h;
    c.Mode = 46;
    c.WhitePoint = 16;
    c.TransferStrength = 3;
    c.ColourStrength = .05f;
    c.MaxRatio = 2;
    c.Passthrough = 1;
    // Warm initial state without sampling its uninitialised previous state.
    f.r.barrier(recon.Get(), UAV, SRV);
    f.dispatch(age.Get(), c, d.Get(), m.Get(), states[0].Get(), (mw + 7) / 8, (mh + 7) / 8, recon.Get(), d.Get());
    f.r.barrier(states[0].Get(), UAV, SRV);
    f.r.barrier(states[1].Get(), UAV, SRV);
    f.r.finish();
    f.uploads.clear();
    c.DebugView = 1;
    DlssNr::Superior::DlssNrConstants dc {};
    dc.Mode = 38;
    dc.Width = w;
    dc.Height = h;
    DlssNr::Superior::Fields::SetPositiveCarrierGain(dc, 1);
    DlssNr::Superior::Fields::SetCarrierSaturation(dc, 1);
    DlssNr::Superior::Fields::SetCarrierNrAnswer(dc, true);
    DlssNr::Superior::Fields::SetCarrierDepthColour(dc, 0, 1, 0, true);
    ComPtr<ID3D12QueryHeap> queries;
    D3D12_QUERY_HEAP_DESC q {};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 2;
    check(f.r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
    auto readback = f.r.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency;
    check(f.r.queue->GetTimestampFrequency(&frequency));
    unsigned index = 0;
    auto measure = [&](bool enabled)
    {
        f.r.begin();
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (enabled)
        {
            f.r.barrier(recon.Get(), SRV, UAV);
            c.Mode = 44;
            f.dispatch(clear.Get(), c, d.Get(), m.Get(), recon.Get(), (mw + 7) / 8, (mh + 7) / 8, nullptr,
                       states[index].Get(), nullptr, nullptr, nullptr, 0, false);
            f.r.barrier(recon.Get(), UAV, SRV);
            f.r.barrier(recon.Get(), SRV, UAV);
            c.Mode = 45;
            f.dispatch(scatter.Get(), c, d.Get(), m.Get(), recon.Get(), (mw + 7) / 8, (mh + 7) / 8, nullptr,
                       states[index].Get(), nullptr, nullptr, nullptr, 1, false);
            f.r.barrier(recon.Get(), UAV, SRV);
            c.Mode = 46;
            unsigned next = 1 - index;
            f.r.barrier(states[next].Get(), SRV, UAV);
            f.dispatch(age.Get(), c, d.Get(), m.Get(), states[next].Get(), (mw + 7) / 8, (mh + 7) / 8, recon.Get(),
                       states[index].Get(), nullptr, nullptr, nullptr, 2, false);
            f.r.barrier(states[next].Get(), UAV, SRV);
            index = next;
        }
        DlssNr::GhostRejection::Settings settings {};
        settings.enabled = enabled;
        settings.method = 2;
        DlssNr::Superior::Fields::SetCarrierGhostRejection(dc, settings);
        f.dispatch(enabled ? decode.Get() : f.carrier.Get(), dc, a.Get(), p.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8,
                   d.Get(), p.Get(), r.Get(), states[index].Get(), nullptr, 3, false);
        // Match the host's decode UAV -> SRV transition on both variants.
        f.r.barrier(out.Get(), UAV, SRV);
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        f.r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        f.r.barrier(out.Get(), SRV, UAV);
        f.r.finish();
        f.uploads.clear();
        UINT64* values;
        check(readback->Map(0, nullptr, reinterpret_cast<void**>(&values)));
        double ms = double(values[1] - values[0]) * 1000 / frequency;
        readback->Unmap(0, nullptr);
        return ms;
    };
    for (unsigned n = 0; n < 16; ++n)
    {
        measure(false);
        measure(true);
    }
    const auto name = std::filesystem::path(stem).filename().string();
    std::ofstream csv("artifacts/ghost-study/" + name + ".temporal-timing.csv");
    csv << "sample,width,height,block,position,variant,ms\n";
    for (unsigned block = 0; block < 16; ++block)
        for (unsigned position = 0; position < 4; ++position)
        {
            bool enabled = position == 1 || position == 2;
            csv << name << ',' << w << ',' << h << ',' << block << ',' << position << ','
                << (enabled ? "temporal" : "plain") << ',' << measure(enabled) << '\n';
        }
    std::cout << "Temporal whole-region timestamps saved for " << name << " (16 warmup pairs + 16 ABBA)\n";
}
