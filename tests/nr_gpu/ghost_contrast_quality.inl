// Independent colour/visibility expectations; no comparison against the same shader.
void GhostContrastQuality(Fixture& f)
{
    using namespace DlssNr::Superior;
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    unsigned checks = 0;
    for (unsigned method : { 2u, 3u, 4u, 5u })
        for (bool full : { false, true })
        {
            const std::string kind = method == 2   ? "temporal"
                                     : method == 3 ? "simple"
                                     : method == 4 ? "fast"
                                                   : "coarse";
            auto ps = f.r.pipeline(dir, "private_carrier_contrast_" + kind + "_" + std::to_string(full));
            for (unsigned metric : { 0u, 1u })
                for (float gain : { .5f, 6.f })
                    for (unsigned channel : { 0u, 1u, 2u })
                        for (float delta : { 0.f, .015625f, .0234375f, .03125f, .25f })
                        {
                            const UINT w = 16, h = 8, step = method == 5 ? 4 : 2, mw = w / step, mh = h / step;
                            const float raw[3] = { .0625f, -.03125f, .015625f },
                                        filtered[3] = { .125f, -.0625f, .09375f };
                            std::vector<float> proxy(w * h * 4, .25f), answer(proxy), encoded(proxy),
                                z(proxy.size(), 0), state(mw * mh * 4, 0), ref(state);
                            for (size_t i = 0; i < proxy.size(); i += 4)
                            {
                                z[i] = .01f;
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    answer[i + c] += (raw[c]);
                                    encoded[i + c] = (full ? .25f : .5f) + filtered[c] * (filtered[c] > 0 ? gain : 1.f);
                                }
                            }
                            for (size_t i = 0; i < state.size(); i += 4)
                            {
                                if (method >= 3)
                                {
                                    uint32_t bits;
                                    float depth = .01f;
                                    std::memcpy(&bits, &depth, 4);
                                    bits = (bits & 0xffffff80u) | 4;
                                    std::memcpy(&state[i], &bits, 4);
                                }
                                else
                                {
                                    state[i] = .01f;
                                    state[i + 1] = 4;
                                }
                                for (unsigned c = 0; c < 3; ++c)
                                    ref[i + c] = raw[c] - (c == channel ? delta : 0);
                                ref[i + 3] = 1;
                                if (method == 5)
                                    state[i] = 1;
                            }
                            f.r.begin();
                            auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer),
                                 e = f.texture(w, h, &encoded), d = f.texture(w, h, &z, DXGI_FORMAT_R32_FLOAT),
                                 s = f.texture(mw, mh, &state,
                                               method == 5   ? DXGI_FORMAT_R16_FLOAT
                                               : method >= 3 ? DXGI_FORMAT_R32G32_UINT
                                                             : DXGI_FORMAT_R32G32B32A32_FLOAT),
                                 r = f.texture(mw, mh, &ref, DXGI_FORMAT_R16G16B16A16_FLOAT), out = f.texture(w, h);
                            DlssNr::Superior::DlssNrConstants c {};
                            c.Mode = 38;
                            c.Width = w;
                            c.Height = h;
                            Fields::SetPositiveCarrierGain(c, gain);
                            Fields::SetCarrierNrAnswer(c, full);
                            Fields::SetCarrierDepthConvention(c, true);
                            DlssNr::GhostRejection::Settings settings { true, .01f, .02f, .75f, false, method };
                            settings.historyFade = 0;
                            settings.contrastEnabled = true;
                            settings.contrastMetric = metric;
                            settings.contrastThreshold = .015625f;
                            settings.contrastSoftness = .015625f;
                            Fields::SetCarrierGhostRejection(c, settings);
                            Fields::SetGhostDepthPrecision(c, 0);
                            Fields::SetUnfilteredEditWeight(c, .25f);
                            f.dispatch(ps.Get(), c, e.Get(), r.Get(), out.Get(), 2, 1, d.Get(), p.Get(), a.Get(),
                                       s.Get());
                            auto values = f.read(out.Get());
                            const double channelWeights[3] = { .2126, .7152, .0722 };
                            const double distance = double(delta) * (metric == 1 ? channelWeights[channel] : 1.);
                            const double x = std::clamp((distance - .015625) / .015625, 0., 1.);
                            const double rejection = .75 * (1 - x * x * (3 - 2 * x)), rawWeight = .25 + .75 * rejection;
                            for (size_t i = 0; i < values.size(); i += 4)
                            {
                                Near(values[i + 3], float(.75 * rejection), "contrast alpha/global fade");
                                for (unsigned ch = 0; ch < 3; ++ch)
                                    Near(values[i + ch], float(filtered[ch] * (1 - rawWeight) + raw[ch] * rawWeight),
                                         "contrast RGB/gain/full-answer");
                            }
                            ++checks;
                        }
        }
    // Equal-brightness colour changes must KEEP the mask only in metric 1.
    // Values are exactly binary16 representable; small brightness mismatch from weighting is <.001.
    for (unsigned method : { 2u, 3u, 4u, 5u })
        for (bool full : { false, true })
            for (unsigned metric : { 0u, 1u, 99u, 100u })
                for (int scenario : { 0, 1, 2 })
                {
                    const UINT w = 16, h = 8, step = method == 5 ? 4 : 2, mw = w / step, mh = h / step;
                    std::vector<float> proxy(w * h * 4, .25f), after(proxy.size(), full ? .375f : .625f),
                        depth(proxy.size(), .01f), state(mw * mh * 4, 0), reference(state);
                    for (size_t i = 0; i < state.size(); i += 4)
                    {
                        if (method >= 3)
                        {
                            uint32_t bits;
                            float z = .01f;
                            std::memcpy(&bits, &z, 4);
                            bits = (bits & 0xffffff80u) | 4;
                            std::memcpy(&state[i], &bits, 4);
                        }
                        else
                        {
                            state[i] = .01f;
                            state[i + 1] = 4;
                        }
                        if (method == 5)
                            state[i] = 1;
                        const float sign = scenario == 1 ? -1.f : 1.f;
                        reference[i] = scenario == 2 ? .0625f : sign * -.5f;
                        reference[i + 1] = scenario == 2 ? .0625f : sign * .148681640625f;
                        reference[i + 2] = scenario == 2 ? .0625f : 0.f;
                        reference[i + 3] = 1;
                    }
                    f.r.begin();
                    auto p = f.texture(w, h, &proxy), e = f.texture(w, h, &after),
                         d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                         guide = f.texture(mw, mh, &state,
                                           method == 5   ? DXGI_FORMAT_R16_FLOAT
                                           : method >= 3 ? DXGI_FORMAT_R32G32_UINT
                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT),
                         r = f.texture(mw, mh, &reference, DXGI_FORMAT_R16G16B16A16_FLOAT), out = f.texture(w, h);
                    DlssNr::Superior::DlssNrConstants c {};
                    c.Mode = 38;
                    c.Width = w;
                    c.Height = h;
                    Fields::SetPositiveCarrierGain(c, 1);
                    Fields::SetCarrierDepthConvention(c, true);
                    Fields::SetCarrierNrAnswer(c, full);
                    DlssNr::GhostRejection::Settings settings { true, .01f, .02f, 1, false, method };
                    settings.historyFade = 0;
                    settings.contrastEnabled = true;
                    settings.contrastMetric = metric;
                    settings.contrastThreshold = .001f;
                    settings.contrastSoftness = 0;
                    Fields::SetCarrierGhostRejection(c, settings);
                    if (metric == 100)
                        c.ExposureAnchors[15] = NAN; // Shader fallback independently of host sanitation.
                    const std::string kind = method == 2   ? "temporal"
                                             : method == 3 ? "simple"
                                             : method == 4 ? "fast"
                                                           : "coarse";
                    auto ps = f.r.pipeline(dir, "private_carrier_contrast_" + kind + "_" + std::to_string(full));
                    f.dispatch(ps.Get(), c, e.Get(), r.Get(), out.Get(), 2, 1, d.Get(), p.Get(), p.Get(), guide.Get());
                    auto values = f.read(out.Get());
                    const bool keepMask = metric == 1 && scenario != 2;
                    for (size_t i = 0; i < values.size(); i += 4)
                    {
                        Near(values[i + 3], keepMask ? 1.f : 0.f, "equal brightness / RGB / invalid selector mask");
                        for (unsigned ch = 0; ch < 3; ++ch)
                            Near(values[i + ch], keepMask ? 0.f : .125f, "equal brightness / neutral contrast output");
                    }
                    ++checks;
                }
    // Invalid history fails open; opposite references cannot average into an artificial match.
    for (bool invalid : { false, true })
    {
        const UINT w = 16, h = 8, mw = 4, mh = 2;
        std::vector<float> proxy(w * h * 4, .25f), after(proxy.size(), .625f), depth(proxy.size(), .01f),
            field(mw * mh * 4, 1), reference(mw * mh * 4, 0);
        for (UINT y = 0; y < mh; ++y)
            for (UINT x = 0; x < mw; ++x)
            {
                size_t i = (y * mw + x) * 4;
                reference[i] = x & 1 ? .25f : -.25f;
                reference[i + 3] = invalid ? 0.f : 1.f;
            }
        f.r.begin();
        auto p = f.texture(w, h, &proxy), e = f.texture(w, h, &after),
             d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
             guide = f.texture(mw, mh, &field, DXGI_FORMAT_R16_FLOAT),
             r = f.texture(mw, mh, &reference, DXGI_FORMAT_R16G16B16A16_FLOAT), out = f.texture(w, h);
        DlssNr::Superior::DlssNrConstants c {};
        c.Mode = 38;
        c.Width = w;
        c.Height = h;
        Fields::SetPositiveCarrierGain(c, 1);
        Fields::SetCarrierDepthConvention(c, true);
        DlssNr::GhostRejection::Settings settings { true, .01f, .02f, 1, false, 5 };
        settings.contrastEnabled = true;
        settings.contrastThreshold = .01f;
        settings.contrastSoftness = 0;
        Fields::SetCarrierGhostRejection(c, settings);
        auto ps = f.r.pipeline(dir, "private_carrier_contrast_coarse_0");
        f.dispatch(ps.Get(), c, e.Get(), r.Get(), out.Get(), 2, 1, d.Get(), p.Get(), p.Get(), guide.Get());
        auto values = f.read(out.Get());
        for (size_t i = 0; i < values.size(); i += 4)
        {
            Near(values[i + 3], 0, "invalid/opposite contrast mask");
            Near(values[i], .125f, "invalid/opposite preserves filtered edit");
        }
        ++checks;
    }
    // Re-select the depth-matched sample, retain the ORIGINAL occluder throughout N, invalidate warmup/reset.
    for (unsigned method : { 2u, 3u, 5u })
        for (bool inverted : { false, true })
            for (float age : { 0.f, 3.f, 4.f })
                for (bool previous : { false, true })
                    for (auto size : { std::pair<UINT, UINT> { 16, 8 }, { 17, 9 }, { 1, 1 } })
                    {
                        const UINT step = method == 5 ? 4 : 2, w = size.first, h = size.second,
                                   mw = (w + step - 1) / step, mh = (h + step - 1) / step;
                        std::vector<float> state(mw * mh * 4, 0), oldRaw(state), oldRef(state), proxy(w * h * 4, .5f),
                            answer(proxy), depth(proxy.size(), inverted ? .01f : .99f);
                        for (UINT y = 0; y < h; ++y)
                            for (UINT x = 0; x < w; ++x)
                            {
                                size_t i = (y * w + x) * 4;
                                depth[i] = (x == std::min((x / step + 1) * step, w) - 1 &&
                                            y == std::min((y / step + 1) * step, h) - 1)
                                               ? (inverted ? .03f : .97f)
                                               : depth[i];
                                answer[i] += (float(x + 1) / 64);
                            }
                        for (UINT y = 0; y < mh; ++y)
                            for (UINT x = 0; x < mw; ++x)
                            {
                                size_t i = (y * mw + x) * 4;
                                if (method >= 3)
                                {
                                    uint32_t z, m;
                                    float zz = .03f;
                                    std::memcpy(&z, &zz, 4);
                                    z = (z & 0xffffff80u) | uint32_t(age);
                                    m = DirectX::PackedVector::XMConvertFloatToHalf(float(step));
                                    std::memcpy(&state[i], &z, 4);
                                    std::memcpy(&state[i + 1], &m, 4);
                                }
                                else
                                {
                                    state[i] = .03f;
                                    state[i + 1] = age;
                                    state[i + 2] = float(step);
                                }
                                oldRaw[i] = float(x + 1) / 32;
                                oldRaw[i + 3] = 1;
                                oldRef[i] = -.125f;
                                oldRef[i + 3] = 1;
                            }
                        f.r.begin();
                        auto s = f.texture(mw, mh, &state,
                                           method >= 3 ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT),
                             o = f.texture(mw, mh, &oldRaw, DXGI_FORMAT_R16G16B16A16_FLOAT),
                             r = f.texture(mw, mh, &oldRef, DXGI_FORMAT_R16G16B16A16_FLOAT),
                             p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer),
                             d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                             newRaw = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
                             newRef = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
                        DlssNr::Superior::DlssNrConstants c {};
                        DlssNr::GhostRejection::Settings settings {};
                        settings.historyFrames = 4;
                        settings.method = method;
                        Fields::SetGhostVisibility(c, mw, mh, w, h, settings, inverted, previous, 0);
                        Fields::SetGhostContrastHistory(c, method, previous);
                        auto ps = f.r.pipeline(dir, method == 5   ? "private_ghost_contrast_coarse"
                                                    : method >= 3 ? "private_ghost_contrast_compact"
                                                                  : "private_ghost_contrast_history");
                        f.dispatch(ps.Get(), c, s.Get(), o.Get(), newRaw.Get(), (mw + 7) / 8, (mh + 7) / 8, p.Get(),
                                   r.Get(), a.Get(), d.Get(), nullptr, 0, true, newRef.Get());
                        auto rawValues = f.read(newRaw.Get());
                        f.r.begin();
                        auto refs = f.read(newRef.Get());
                        for (UINT y = 0; y < mh; ++y)
                            for (UINT x = 0; x < mw; ++x)
                            {
                                size_t i = (y * mw + x) * 4;
                                Near(rawValues[i], float(std::min(x * step + step, w)) / 64,
                                     "depth-selected raw before transport");
                                const bool valid = previous && age > 0 && x + 1 < mw;
                                Near(refs[i + 3], valid ? 1.f : 0.f, "reference reset/edge/expiry validity");
                                Near(refs[i], valid ? (age == 4 ? float(x + 2) / 32 : -.125f) : 0.f,
                                     "original occluder retained across window");
                            }
                        ++checks;
                    }
    // Soft dilation carries the same winning occluder as its maximum timer.
    for (unsigned radius : { 1u, 2u })
    {
        constexpr UINT w = 5, h = 5;
        std::vector<float> state(w * h * 4, 0), ref(state);
        uint32_t z;
        float depth = .03f;
        std::memcpy(&z, &depth, 4);
        z = (z & 0xffffff80u) | 4;
        std::memcpy(&state[(2 * w + 2) * 4], &z, 4);
        ref[(2 * w + 2) * 4] = -.125f;
        ref[(2 * w + 2) * 4 + 3] = 1;
        f.r.begin();
        auto s = f.texture(w, h, &state, DXGI_FORMAT_R32G32_UINT),
             r = f.texture(w, h, &ref, DXGI_FORMAT_R16G16B16A16_FLOAT),
             field = f.texture(w, h, nullptr, DXGI_FORMAT_R16_FLOAT),
             expanded = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
        DlssNr::Superior::DlssNrConstants c {};
        DlssNr::GhostRejection::Settings settings {};
        settings.coarseRadius = radius;
        settings.historyFade = 0;
        Fields::SetCoarseGhostDilate(c, w, h, settings);
        c.Mode = 53;
        auto ps = f.r.pipeline(dir, "private_ghost_contrast_dilate_" + std::to_string(radius));
        f.dispatch(ps.Get(), c, s.Get(), r.Get(), field.Get(), 1, 1, nullptr, nullptr, nullptr, nullptr, nullptr, 0,
                   true, expanded.Get());
        auto mask = f.read(field.Get());
        f.r.begin();
        auto refs = f.read(expanded.Get());
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                size_t i = (y * w + x) * 4;
                bool active = std::abs(int(x) - 2) <= int(radius) && std::abs(int(y) - 2) <= int(radius);
                Near(mask[i], active ? 1.f : 0.f, "contrast dilation age");
                Near(refs[i], active ? -.125f : 0.f, "dilated reference");
                Near(refs[i + 3], active ? 1.f : 0.f, "dilated reference validity");
            }
        ++checks;
    }
    std::cout << "Disocclusion edit contrast PASS: " << checks
              << " RGB/brightness/threshold/softness/gain/full-answer/history/reset/guide/dilation cases\n";
}
