// Independent double-precision scalar reference with mixed surfaces/invalid history and HDR.
void CompactFastStressQuality(Fixture& f)
{
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    auto editPs = f.r.pipeline(dir, "private_carrier_compact_fast"),
         answerPs = f.r.pipeline(dir, "private_carrier_compact_fast_answer"),
         fadePs = f.r.pipeline(dir, "private_carrier_compact_fast_fade");
    unsigned cases = 0;
    for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 2, 2 }, { 3, 5 }, { 17, 9 }, { 1281, 7 } })
        for (unsigned seed = 0; seed < 24; ++seed)
        {
            const unsigned w = size.first, h = size.second, mw = (w + 1) / 2, mh = (h + 1) / 2;
            const bool full = (seed & 1) != 0, reversed = (seed & 2) != 0;
            const float gains[] = { .01f, .5f, 1, 6, 1024 };
            const float gain = gains[seed % 5], strength = seed % 3 == 0 ? .7f : 1.f;
            const unsigned fade = seed % 3 == 0 ? 0 : seed % 3 == 1 ? 3 : 120;
            const float expansion = float(seed % 3) * .5f;
            const float rawFades[] = { 0.f, .35f, 1.f, NAN, INFINITY, -.4f };
            const float globalRequested = rawFades[(seed / 2) % 6];
            const float globalRaw = std::isfinite(globalRequested) ? std::clamp(globalRequested, 0.f, 1.f) : 0.f;

            const float precision = seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1.f / 65535.f : 1.f / 16777215.f;
            std::vector<float> proxy(w * h * 4), answer(proxy), encoded(proxy), depth(w * h * 4), state(mw * mh * 4, 0);
            std::vector<float> zs(mw * mh);
            std::vector<unsigned> ages(mw * mh);
            for (unsigned i = 0; i < mw * mh; ++i)
            {
                float z = .01f * float(1 + (i * 7 + seed) % 20);
                if (i % 17 == 0)
                    z = NAN;
                if (i % 17 == 1)
                    z = INFINITY;
                if (i % 17 == 2)
                    z = -.1f;
                unsigned bits;
                std::memcpy(&bits, &z, 4);
                bits &= 0xffffff80u;
                std::memcpy(&zs[i], &bits, 4);
                ages[i] = (i * 13 + seed * 19) % (fade == 120 ? 121 : 7);
                bits |= ages[i];
                std::memcpy(&state[i * 4], &bits, 4);
            }
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const size_t i = (y * w + x) * 4;
                    float z = .01f * float(1 + (x * 3 + y * 7 + seed) % 20);
                    depth[i] = reversed ? z : 1 - z;
                    if (precision > 0)
                        depth[i] = std::round(depth[i] / precision) * precision;
                    if ((x + y + seed) % 29 == 0)
                        depth[i] = NAN;
                    if ((x + y + seed) % 29 == 1)
                        depth[i] = INFINITY;
                    if ((x + y + seed) % 29 == 2)
                        depth[i] = -1;
                    for (unsigned ch = 0; ch < 3; ++ch)
                    {
                        const float scale = seed % 4 == 0 ? 24000.f : .03f;
                        proxy[i + ch] = seed % 4 == 0 ? 4096.f : .2f;
                        const float raw = (int((x + y + ch + seed) % 7) - 3) * scale;
                        const float filtered = (int((x * 3 + y + ch + seed) % 5) - 2) * scale;
                        answer[i + ch] = proxy[i + ch] + raw;
                        encoded[i + ch] = (full ? proxy[i + ch] : .5f) + filtered * (filtered > 0 ? gain : 1.f);
                        if ((x + ch + seed) % 31 == 0)
                            answer[i + ch] = NAN;
                        if ((x + ch + seed) % 31 == 1)
                            encoded[i + ch] = INFINITY;
                    }
                }
            f.r.begin();
            auto p = f.texture(w, h, &proxy), a = f.texture(w, h, &answer), e = f.texture(w, h, &encoded),
                 d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                 s = f.texture(mw, mh, &state, DXGI_FORMAT_R32G32_UINT), out = f.texture(w, h);
            DlssNr::Superior::DlssNrConstants c {};
            c.Mode = 38;
            c.Width = w;
            c.Height = h;
            DlssNr::Superior::Fields::SetPositiveCarrierGain(c, gain);
            DlssNr::Superior::Fields::SetCarrierNrAnswer(c, full);
            DlssNr::Superior::Fields::SetCarrierDepthConvention(c, reversed);
            DlssNr::GhostRejection::Settings settings {};
            settings.enabled = true;
            settings.method = 4;
            settings.strength = strength;
            settings.historyFade = fade;
            settings.historyFrames = 120;
            settings.softExpansion = expansion;
            DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
            DlssNr::Superior::Fields::SetGhostDepthPrecision(c, precision);
            DlssNr::Superior::Fields::SetUnfilteredEditWeight(c, globalRequested);
            f.dispatch(full ? answerPs.Get() : (globalRaw > 0 ? fadePs.Get() : editPs.Get()), c, e.Get(), p.Get(),
                       out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(), a.Get(), s.Get());
            auto got = f.read(out.Get());
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const size_t i = (y * w + x) * 4;
                    const float device = depth[i];
                    const double nearZ =
                        std::isfinite(device) && device >= 0 && device <= 1 ? (reversed ? device : 1.f - device) : 0;
                    double sum = 0, weights = 0, broadened = 0;
                    const double px = (x + .5) * mw / w - .5, py = (y + .5) * mh / h - .5;
                    const int bx = int(std::floor(px)), by = int(std::floor(py));
                    if (nearZ > 0)
                        for (int yy = 0; yy < 2; ++yy)
                            for (int xx = 0; xx < 2; ++xx)
                            {
                                const unsigned j =
                                    std::clamp(by + yy, 0, int(mh) - 1) * mw + std::clamp(bx + xx, 0, int(mw) - 1);
                                const double z = zs[j], maxZ = std::max(z, nearZ);
                                const double margin = std::max(std::max(double(precision) * 2, 1e-6), maxZ / 65536.);
                                if (!std::isfinite(z) || z <= 0 || std::abs(z - nearZ) > maxZ * .05 + margin)
                                    continue;
                                const double tx = xx ? px - bx : 1 - (px - bx), ty = yy ? py - by : 1 - (py - by);
                                const double b = tx * ty;
                                sum += ages[j] * b;
                                weights += b;
                                broadened = std::max(broadened, ages[j] * std::min(tx, ty));
                            }
                    double mask = 0;
                    if (weights > 1e-6 && sum > 0)
                    {
                        const double base = sum / weights, age = std::max(base, base + (broadened - base) * expansion);
                        mask = strength * (fade ? std::min(1., age / fade) : 1.);
                    }
                    if (!std::isfinite(got[i + 3]) || std::abs(got[i + 3] - mask * (1 - globalRaw)) > 3e-6)
                        throw std::runtime_error("Fast compact mixed-depth mask reference");
                    for (unsigned ch = 0; ch < 3; ++ch)
                    {
                        const float base = full ? proxy[i + ch] : .5f;
                        const float value = std::isfinite(encoded[i + ch]) ? encoded[i + ch] : base;
                        const float delta = value - base;
                        const double filtered = std::min(delta, 0.f) + double(std::max(delta, 0.f)) / gain;
                        const float raw = std::isfinite(answer[i + ch]) ? answer[i + ch] - proxy[i + ch] : 0;
                        const double rejected = filtered * (1 - mask) + raw * mask;
                        const double expected = rejected * (1 - globalRaw) + raw * globalRaw;
                        const double bound = 1e-5 + 3e-6 * std::max(std::abs(filtered), double(std::abs(raw)));
                        if (!std::isfinite(got[i + ch]) || std::abs(got[i + ch] - expected) > bound)
                            throw std::runtime_error("Fast compact HDR/mixed-depth RGB reference");
                    }
                }
            ++cases;
        }
    std::cout << "Fast compact stress PASS: " << cases
              << " independent mixed-depth/NaN/Inf/D16/D24/HDR/odd-grid/gain/global-fade cases\n";
}

// Independent scalar splat plus the retained method-4 PSO. Adversarial nonuniform
// destinations exercise the wave fallback, partial waves and inactive lanes.
void CompactWaveScatterQuality(Fixture& f)
{
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    auto clear = f.r.pipeline(dir, "private_ghost_clear"), reference = f.r.pipeline(dir, "private_ghost_compact_1"),
         wave = f.r.pipeline(dir, "private_ghost_compact_wave");
    unsigned cases = 0;
    for (auto size : { std::pair<UINT, UINT> { 1, 1 }, { 2, 2 }, { 3, 5 }, { 17, 9 }, { 33, 19 }, { 129, 67 } })
        for (unsigned seed = 0; seed < 12; ++seed)
        {
            const unsigned w = size.first, h = size.second, mw = (w + 1) / 2, mh = (h + 1) / 2;
            const bool reversed = (seed & 1) != 0, initialized = seed % 4 != 0;
            std::vector<float> depth(w * h * 4), motion(depth.size(), 0);
            std::vector<uint32_t> expected(mw * mh, 0);
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    size_t i = (y * w + x) * 4;
                    float z = float(1 + (x * 13 + y * 7 + seed) % 100) / 1024;
                    depth[i] = reversed ? z : 1 - z;
                    if (seed % 3)
                    {
                        const float q = seed % 3 == 1 ? 65535.f : 16777215.f;
                        depth[i] = std::round(depth[i] * q) / q;
                    }
                    if ((x + y + seed) % 37 == 0)
                        depth[i] = NAN;
                    if ((x + y + seed) % 37 == 1)
                        depth[i] = INFINITY;
                    if ((x + y + seed) % 37 == 2)
                        depth[i] = -1;
                    motion[i] = seed % 3 == 0 ? 3.25f : .125f * float(int((x * 17 + y + seed) % 257) - 128);
                    motion[i + 1] = seed % 3 == 0 ? -1.5f : .125f * float(int((x + y * 19 + seed) % 257) - 128);
                    if ((x + y + seed) % 41 == 0)
                        motion[i] = INFINITY;
                    if ((x + y + seed) % 41 == 1)
                        motion[i] = NAN;
                    if ((x + y + seed) % 41 == 2)
                        motion[i] = 65504.f;
                    if ((x + y + seed) % 41 == 3)
                        motion[i] = -32767.875f;
                }
            if (initialized)
                for (unsigned y = 0; y < mh; ++y)
                    for (unsigned x = 0; x < mw; ++x)
                    {
                        float nearest = -1;
                        unsigned qx = std::min(x * 2, w - 1), qy = std::min(y * 2, h - 1);
                        for (unsigned yy = 0; yy < 2; ++yy)
                            for (unsigned xx = 0; xx < 2; ++xx)
                            {
                                unsigned sx = std::min(x * 2 + xx, w - 1), sy = std::min(y * 2 + yy, h - 1);
                                const float value = depth[(sy * w + sx) * 4];
                                const float n = std::isfinite(value) && value >= 0 && value <= 1
                                                    ? (reversed ? value : 1 - value)
                                                    : -1;
                                if (n > nearest)
                                {
                                    nearest = n;
                                    qx = sx;
                                    qy = sy;
                                }
                            }
                        const size_t i = (qy * w + qx) * 4;
                        const float vx = motion[i], vy = motion[i + 1];
                        if (nearest <= 0 || !std::isfinite(vx) || !std::isfinite(vy) || std::abs(vx) >= 65504 ||
                            std::abs(vy) >= 65504)
                            continue;
                        // Eighth-pixel velocities keep these reference footprints exact in binary arithmetic.
                        const double px = (qx + .5 + vx) * .5 - .5, py = (qy + .5 + vy) * .5 - .5;
                        const int bx = int(std::floor(px)), by = int(std::floor(py));
                        uint32_t bits;
                        std::memcpy(&bits, &nearest, 4);
                        for (int yy = 0; yy < 2; ++yy)
                            for (int xx = 0; xx < 2; ++xx)
                            {
                                int tx = bx + xx, ty = by + yy;
                                const double wx = xx ? px - bx : 1 - (px - bx), wy = yy ? py - by : 1 - (py - by);
                                if (tx >= 0 && ty >= 0 && tx < int(mw) && ty < int(mh) && wx * wy > .05)
                                    expected[ty * mw + tx] = std::max(expected[ty * mw + tx], bits);
                            }
                    }
            std::vector<float> previousWords;
            for (unsigned variant = 0; variant < 2; ++variant)
            {
                f.r.begin();
                auto d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                     m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT),
                     recon = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT),
                     state = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32G32_UINT);
                DlssNr::Superior::DlssNrConstants c {};
                c.Mode = 44;
                c.Width = mw;
                c.Height = mh;
                c.GuideWidth = w;
                c.GuideHeight = h;
                c.Passthrough = reversed;
                c.DebugView = initialized;
                f.dispatch(clear.Get(), c, d.Get(), m.Get(), recon.Get(), (mw + 7) / 8, (mh + 7) / 8);
                f.r.barrier(recon.Get(), UAV, SRV);
                f.r.barrier(recon.Get(), SRV, UAV);
                c.Mode = 47;
                f.dispatch(variant ? wave.Get() : reference.Get(), c, d.Get(), m.Get(), recon.Get(), (mw + 7) / 8,
                           (mh + 7) / 8, nullptr, nullptr, nullptr, nullptr, nullptr, 0, false, state.Get());
                auto got = f.read(recon.Get());
                for (size_t i = 0; i < expected.size(); ++i)
                {
                    uint32_t bits;
                    std::memcpy(&bits, &got[i * 4], 4);
                    if (bits != expected[i])
                        throw std::runtime_error("Wave splat reference mismatch variant=" + std::to_string(variant) +
                                                 " seed=" + std::to_string(seed) + " w=" + std::to_string(w) +
                                                 " h=" + std::to_string(h) + " pixel=" + std::to_string(i) + " got=" +
                                                 std::to_string(bits) + " expected=" + std::to_string(expected[i]));
                }
                f.r.begin();
                auto words = f.read(state.Get());
                if (!variant)
                    previousWords = std::move(words);
                else if (previousWords.size() != words.size() ||
                         std::memcmp(previousWords.data(), words.data(), words.size() * sizeof(float)))
                    throw std::runtime_error("Wave scatter changed current guide words");
            }
            ++cases;
        }
    std::cout << "Compact wave scatter PASS: " << cases
              << " independent scalar/retained-PSO cases; partial waves, divergent MV, D16/D24, invalid and far-out "
                 "destinations\n";
}
