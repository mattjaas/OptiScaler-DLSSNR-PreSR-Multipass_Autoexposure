// Production guided decode, constructed ghost injection and protected-control cases.
void GuidedGhostQuality(Fixture& f)
{
    const UINT w = 64, h = 48;
    auto ps = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_guided_ghost");
    unsigned cases = 0;
    for (unsigned scenario = 0; scenario < 8; ++scenario)
        for (bool reversed : { false, true })
        {
            std::vector<float> raw(w * h * 4), after(raw), proxy(raw), depth(raw), motion(raw);
            for (UINT y = 0; y < h; ++y)
                for (UINT x = 0; x < w; ++x)
                {
                    size_t i = (y * w + x) * 4;
                    bool object = x >= 28 && x < 40 && y >= 8 && y < 40;
                    float z = object ? .03f : .01f;
                    if (scenario == 1)
                        z = .01f; // same surface: preserve intended temporal changes
                    if (scenario == 5)
                        z = object ? .005f : .03f; // farther surface cannot contaminate nearer one
                    if (scenario == 6)
                        z = NAN;
                    depth[i] = reversed ? z : 1 - z;
                    motion[i] = object ? 0 : 6;
                    if (scenario == 2)
                        motion[i] = 0; // no relative motion evidence
                    if (scenario == 4)
                        motion[i] = object ? 6 : 0; // moving object, stationary camera/background
                    if (scenario == 7)
                        motion[i] = INFINITY;
                    for (unsigned ch = 0; ch < 3; ++ch)
                    {
                        const float colour[] = { .20f, -.06f, .09f };
                        raw[i + ch] = object ? colour[ch] : 0.f;
                        after[i + ch] = raw[i + ch];
                        if (x >= 16 && x < 28 && y >= 10 && y < 38)
                            after[i + ch] = .4f * colour[ch]; // known displaced transparent copy of foreground
                        if (scenario == 3)
                        {
                            raw[i + ch] += ((x + y) % 2 ? .02f : -.02f);
                            after[i + ch] = object ? colour[ch] : 0.f; // valid noise suppression, no injected ghost
                        }
                    }
                }
            f.r.begin();
            auto r = f.texture(w, h, &raw), a = f.texture(w, h, &after), p = f.texture(w, h, &proxy),
                 d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                 m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT), out = f.texture(w, h);
            DlssNr::Superior::DlssNrConstants c {};
            c.Mode = 38;
            c.Width = w;
            c.Height = h;
            DlssNr::Superior::Fields::SetPositiveCarrierGain(c, 1);
            DlssNr::Superior::Fields::SetCarrierSaturation(c, 1);
            DlssNr::Superior::Fields::SetCarrierNrAnswer(c, true);
            DlssNr::Superior::Fields::SetCarrierDepthColour(c, 0, 1, 0, reversed);
            DlssNr::GhostRejection::Settings settings { true, .004f, .01f, 1, false, 1 };
            DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
            DlssNr::Superior::Fields::SetGhostDepthPrecision(c, 1.f / 65535.f);
            f.dispatch(ps.Get(), c, a.Get(), p.Get(), out.Get(), w / 8, h / 8, d.Get(), p.Get(), r.Get(), m.Get());
            auto got = f.read(out.Get());
            const size_t ghost = (24 * w + 22) * 4, interior = (24 * w + 33) * 4;
            if (scenario == 0 || scenario == 4)
            {
                if (got[ghost + 3] < .9f || std::abs(got[ghost]) > .008f)
                    throw std::runtime_error("Guided detector missed known injected ghost, case=" +
                                             std::to_string(scenario));
            }
            else if (got[ghost + 3] != 0.f)
                throw std::runtime_error("Guided detector changed protected temporal control, case=" +
                                         std::to_string(scenario));
            if (got[interior + 3] != 0.f)
                throw std::runtime_error("Current foreground interior must be protected");
            for (float v : got)
                if (!std::isfinite(v))
                    throw std::runtime_error("Guided detector nonfinite output");
            ++cases;
        }
    std::cout << "Guided ghost rejection PASS: " << cases
              << " injected/camera-static/equal-depth/no-motion/noise/farther/invalid cases\n";
}

std::vector<float> GuidedPlane(const std::string& path, unsigned channels, unsigned format, unsigned& w, unsigned& h)
{
    std::ifstream stream(path, std::ios::binary);
    std::array<uint32_t, 37> header {};
    stream.read(reinterpret_cast<char*>(header.data()), 148);
    if (!stream || header[0] != 0x20534444 || header[21] != 0x30315844 || header[32] != format)
        throw std::runtime_error("Guided sample format: " + path);
    w = header[4];
    h = header[3];
    if (!w || !h || w > 16384 || h > 16384 || header[5] != w * channels * 4)
        throw std::runtime_error("Guided sample dimensions");
    std::vector<float> tight(size_t(w) * h * channels), rgba(size_t(w) * h * 4, 0);
    stream.read(reinterpret_cast<char*>(tight.data()), tight.size() * 4);
    if (!stream)
        throw std::runtime_error("Truncated guided sample");
    for (size_t i = 0; i < size_t(w) * h; ++i)
        for (unsigned ch = 0; ch < channels; ++ch)
            rgba[i * 4 + ch] = tight[i * channels + ch];
    return rgba;
}
void GuidedGhostCapture(Fixture& f, const std::string& stem, bool timing = false)
{
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || w != dw || w != mw || h != ah || h != dh || h != mh)
        throw std::runtime_error("Unmatched guided sample planes");
    std::vector<float> zero(raw.size(), 0);
    f.r.begin();
    auto r = f.texture(w, h, &raw), a = f.texture(w, h, &after), p = f.texture(w, h, &zero),
         d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT), m = f.texture(w, h, &motion, DXGI_FORMAT_R32G32_FLOAT),
         out = f.texture(w, h);
    DlssNr::Superior::DlssNrConstants c {};
    c.Mode = 38;
    c.Width = w;
    c.Height = h;
    DlssNr::Superior::Fields::SetPositiveCarrierGain(c, 1);
    DlssNr::Superior::Fields::SetCarrierSaturation(c, 1);
    DlssNr::Superior::Fields::SetCarrierNrAnswer(c, true);
    DlssNr::Superior::Fields::SetCarrierDepthColour(c, 0, 1, 0, true);
    DlssNr::Superior::Fields::SetCarrierGhostRejection(c, { true, .004f, .01f, 1, false, 1 });
    DlssNr::Superior::Fields::SetGhostDepthPrecision(c, 0);
    auto ps = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_guided_ghost");
    f.dispatch(ps.Get(), c, a.Get(), p.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(), r.Get(), m.Get());
    const auto got = f.read(out.Get());
    const auto name = std::filesystem::path(stem).filename().string();
    std::ofstream output("artifacts/ghost-study/" + name + ".guided.dds", std::ios::binary);
    if (!DlssNr::EditDump::WriteRows(w, h, got.data(), size_t(w) * 16,
                                     [&](const void* data, size_t bytes)
                                     {
                                         output.write(static_cast<const char*>(data), bytes);
                                         return bool(output);
                                     }))
        throw std::runtime_error("Guided sample output failed");
    std::cout << "Guided production shader sample: " << name << '\n';
    if (timing)
    {
        output.close();
        f.r.begin();
        f.r.barrier(out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, UAV);
        auto legacy = f.r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_carrier_ghost");
        std::ofstream csv("artifacts/ghost-study/" + name + ".timing.csv");
        csv << "sample,width,height,baseline,block,position,variant,ms\n";
        // Recycle one UAV between complete submissions. No allocations enter GPU query intervals.
        for (unsigned base = 0; base < 2; ++base)
        {
            auto measure = [&](bool candidate)
            {
                auto cc = c;
                if (!candidate)
                    DlssNr::Superior::Fields::SetCarrierGhostRejection(cc, { base != 0, .004f, .01f, 1, false, 0 });
                double ms;
                f.dispatch(candidate ? ps.Get()
                           : base    ? legacy.Get()
                                     : f.carrier.Get(),
                           cc, a.Get(), p.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(), r.Get(),
                           m.Get(), &ms);
                return ms;
            };
            for (unsigned n = 0; n < 16; ++n)
            {
                measure(false);
                measure(true);
            }
            for (unsigned block = 0; block < 16; ++block)
            {
                unsigned position = 0;
                for (bool candidate : { false, true, true, false })
                    csv << name << ',' << w << ',' << h << ',' << base << ',' << block << ',' << position++ << ','
                        << (candidate ? 'B' : 'A') << ',' << measure(candidate) << '\n';
            }
        }
    }
}
