// Known mixture fractions and independent absence controls, not a shader-vs-itself comparison.
void GhostOutputQuality(Fixture& f)
{
    using namespace DlssNr::Superior;
    unsigned checks = 0;
    for (unsigned mode : { 0u, 1u, 2u })
        for (unsigned method : { 2u, 3u, 4u, 5u })
            for (bool full : { false, true })
                for (float gain : { .5f, 6.f })
                    for (unsigned scenario = 0; scenario < 14; ++scenario)
                    {
                        if (mode && full)
                            continue;
                        constexpr UINT w = 16, h = 8;
                        const UINT step = method == 5 ? 4 : 2, mw = w / step, mh = h / step;
                        const float fractions[] = { 0, .0625f, .1875f, .375f, -.25f };
                        const float raw[3] = { .0625f, -.03125f, .015625f };
                        float filtered[3] = { raw[0], raw[1], raw[2] };
                        if (scenario < 5)
                            filtered[0] += .25f * fractions[scenario];
                        else if (scenario == 5 || scenario == 13)
                            filtered[1] += .0625f; // Perpendicular to the old occluder.
                        else
                            filtered[0] += .09375f;
                        std::vector<float> proxy(w * h * 4, .25f), answer(proxy), encoded(proxy),
                            depth(proxy.size(), .01f), state(mw * mh * 4, 0), reference(state);
                        for (size_t i = 0; i < proxy.size(); i += 4)
                            for (unsigned ch = 0; ch < 3; ++ch)
                                answer[i + ch] += raw[ch];
                        if (scenario == 13)
                        {
                            // A neighbouring current edit can account for the same perpendicular change.
                            for (int y : { 2, 6 })
                                for (int x : { 6, 10 })
                                    answer[(y * w + x) * 4 + 1] = .25f + filtered[1];
                        }
                        TransportFixture t(f, w, h, proxy, answer);
                        std::array<float, 4> coeff { 1, 0, 0, 0 };
                        if (mode)
                            coeff = t.Prepare(mode, gain);
                        if (scenario == 10)
                            filtered[0] = raw[0] + .000001f; // Below the carrier precision floor.
                        if (scenario == 11)
                            filtered[0] = -.03125f; // Cross zero under positive-only gain.
                        for (size_t i = 0; i < proxy.size(); i += 4)
                            for (unsigned ch = 0; ch < 3; ++ch)
                                encoded[i + ch] =
                                    mode == 1   ? .5f + filtered[ch] * coeff[0]
                                    : mode == 2 ? (filtered[ch] - coeff[2]) * coeff[0]
                                                : (full ? .25f : .5f) + filtered[ch] * (filtered[ch] > 0 ? gain : 1);
                        for (size_t i = 0; i < state.size(); i += 4)
                        {
                            if (method == 3 || method == 4)
                            {
                                float z = .01f;
                                uint32_t bits;
                                std::memcpy(&bits, &z, 4);
                                bits = (bits & 0xffffff80u) | 4;
                                std::memcpy(&state[i], &bits, 4);
                            }
                            else
                            {
                                state[i] = method == 5 ? 1.f : .01f;
                                state[i + 1] = 4;
                            }
                            for (unsigned ch = 0; ch < 3; ++ch)
                                reference[i + ch] = raw[ch] + (ch == 0 && scenario != 6 ? .25f : 0);
                            if (scenario == 11)
                                reference[i] = -.1875f;
                            reference[i + 3] = scenario == 7 ? 0.f : 1.f;
                            if (scenario == 12)
                                for (unsigned ch = 0; ch < 4; ++ch)
                                    reference[i + ch] *= .5f; // Weighted history references decode identically.
                        }
                        auto e = f.texture(w, h, &encoded), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                             s = f.texture(mw, mh, &state,
                                           method == 5   ? DXGI_FORMAT_R16_FLOAT
                                           : method >= 3 ? DXGI_FORMAT_R32G32_UINT
                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT),
                             ref = f.texture(mw, mh, &reference, DXGI_FORMAT_R16G16B16A16_FLOAT), out = f.texture(w, h);
                        DlssNr::GhostRejection::Settings settings { true, .01f, .02f, .75f, false, method };
                        settings.historyFade = 0;
                        settings.outputGate = true;
                        settings.outputThreshold = .125f;
                        settings.outputSoftness = .125f;
                        settings.contrastEnabled = scenario == 8;
                        settings.contrastThreshold = 0;
                        settings.contrastSoftness = 0;
                        DlssNr::Superior::DlssNrConstants c {};
                        c.Mode = 38;
                        c.Width = w;
                        c.Height = h;
                        Fields::SetCarrierTransport(c, mode);
                        Fields::SetCarrierNrAnswer(c, full);
                        Fields::SetPositiveCarrierGain(c, gain);
                        Fields::SetCarrierDepthConvention(c, true);
                        Fields::SetCarrierGhostRejection(c, settings);
                        Fields::SetGhostDepthPrecision(c, 0);
                        Fields::SetUnfilteredEditWeight(c, .25f);
                        if (scenario == 9)
                        {
                            c.ExposureAnchors[2] = NAN;
                            c.ExposureAnchors[3] = INFINITY; // Shader-side fallbacks, independent of host.
                        }
                        if (!Fields::GhostEditContrast(c))
                            throw std::runtime_error("Output gate did not select reference-history decode");
                        const std::string kind = method == 2   ? "temporal"
                                                 : method == 3 ? "simple"
                                                 : method == 4 ? "fast"
                                                               : "coarse";
                        auto ps = f.visibilityPipeline(mode ? "private_affine_contrast_" + kind
                                                            : "private_carrier_contrast_" + kind + "_" +
                                                                  std::to_string(full));
                        f.dispatch(ps.Get(), c, e.Get(), ref.Get(), out.Get(), 2, 1, d.Get(), t.proxy.Get(),
                                   t.answer.Get(), s.Get(), nullptr, 0, true, nullptr,
                                   mode ? t.coefficients.Get() : nullptr);
                        auto values = f.read(out.Get());
                        // Midpoint fraction .1875 lies halfway through .125..25; smoothstep = .5.
                        float keep = scenario == 2 ? .5f
                                     : scenario == 3 || scenario == 6 || scenario == 7 || scenario == 9 ||
                                             scenario == 11 || scenario == 12
                                         ? 1.f
                                         : 0.f;
                        if (scenario != 13)
                        {
                            // A uniform current edit has a zero-width envelope in every channel.
                            const auto carrier = [&](double edit)
                            {
                                if (mode == 1)
                                    return std::clamp(.5 + edit * coeff[0], 0., 65504.);
                                if (mode == 2)
                                    return std::clamp((edit - coeff[2]) * coeff[0], 0., 65504.);
                                return std::clamp((full ? .25 : .5) + edit * (edit > 0 ? gain : 1), 0., 65504.);
                            };
                            double deviation = 0, amplitude = 0;
                            for (unsigned ch = 0; ch < 3; ++ch)
                            {
                                deviation = std::max(deviation, std::abs(carrier(filtered[ch]) - carrier(raw[ch])));
                                amplitude = std::max(amplitude, std::abs(carrier(raw[ch]) - carrier(0)));
                            }
                            if (deviation > 1.0 / 1024.0)
                            {
                                double lo = .18, ramp = scenario == 9 ? .05 : .125;
                                double t = std::clamp((deviation / (.02 + amplitude) - lo) / ramp, 0., 1.);
                                keep = std::max(keep, float(t * t * (3 - 2 * t)));
                            }
                        }
                        const float rejection = .75f * keep, weight = .25f + .75f * rejection;
                        for (size_t i = 0; i < values.size(); i += 4)
                        {
                            if (scenario == 13 && i != (4 * w + 8) * 4)
                                continue;
                            Near(values[i + 3], .75f * rejection, "output evidence mask/fade/ambiguity");
                            for (unsigned ch = 0; ch < 3; ++ch)
                                Near(values[i + ch], filtered[ch] * (1 - weight) + raw[ch] * weight,
                                     "output evidence edit with shared RGB weight");
                        }
                        ++checks;
                    }
    std::cout << "Output evidence PASS: " << checks
              << " known mixtures, local support, off-axis trails, ambiguous reference, contrast, "
                 "transport/gain/full-answer, fade, nonfinite fallbacks\n";
}
