// A stationary depth edge cannot uncover anything, even with small stable subpixel guide noise.
void StaticVisibilityQuality(Fixture& f)
{
    unsigned checks = 0;
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
            for (float mv : { 0.f, .125f })
                for (unsigned bits : { 0u, 16u, 24u })
                    for (int left : { 40, 41, 43 })
                    {
                        TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2, variant == 3 ? 4 : 2);
                        t.Frame(left, 12, .03f, mv, mv, false, reversed, false, bits);
                        for (unsigned frame = 0; frame < 3; ++frame)
                        {
                            auto state = t.Frame(left, 12, .03f, mv, mv, false, reversed, false, bits);
                            for (size_t i = 1; i < state.size(); i += 4)
                                if (state[i] != 0)
                                    throw std::runtime_error(
                                        "Stationary depth edge created a disocclusion, variant=" +
                                        std::to_string(variant) + " reversed=" + std::to_string(reversed) +
                                        " motion=" + std::to_string(mv) + " bits=" + std::to_string(bits) +
                                        " left=" + std::to_string(left));
                        }
                        ++checks;
                    }
    std::cout << "Stationary depth-edge visibility PASS: " << checks
              << " legacy/compact/wave/coarse, Z, D16/D24 and subpixel-noise sequences\n";
}
// Known two-surface motions. All expectations come from visibility and the configured MV gate.
void RelativeVisibilityQuality(Fixture& f)
{
    unsigned checks = 0;
    struct Sequence
    {
        int next;
        float bg, fg;
        int pixel;
        bool expected;
    };
    const Sequence sequences[] = {
        { 44, 4, -4, 36, true },     // opposing directions
        { 48, -1, -8, 44, true },    // same direction, foreground faster
        { 41, -8, -1, 48, true },    // same direction, background faster
        { 48, 0, -8, 40, true },     // stationary background
        { 44, 0, -.05f, 40, false }, // below relative speed gate
        { 44, -4, -4, 36, false },   // equal rigid translation
        { 40, 0, 0, 36, false }      // both stationary
    };
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
        {
            const unsigned step = variant == 3 ? 4 : 2;
            for (const auto& seq : sequences)
            {
                TemporalGhostFixture t(f, 128, 64, variant != 0, variant == 2, step);
                t.Frame(40, 4, .03f, seq.bg, seq.fg, false, reversed);
                auto state = t.Frame(seq.next, 4, .03f, seq.bg, seq.fg, false, reversed);
                const float age = state[((24 / step) * t.mw + seq.pixel / step) * 4 + 1];
                if (seq.expected ? age != 4 : age != 0)
                    throw std::runtime_error("Relative layer motion failed variant=" + std::to_string(variant) +
                                             " pixel=" + std::to_string(seq.pixel) + " bg=" + std::to_string(seq.bg) +
                                             " fg=" + std::to_string(seq.fg) + " age=" + std::to_string(age));
                ++checks;
            }
            TemporalGhostFixture start(f, 128, 64, variant != 0, variant == 2, step);
            start.Frame(40, 12, .03f, 0, 0, false, reversed);
            auto started = start.Frame(44, 12, .03f, 0, -4, false, reversed);
            if (started[((24 / step) * start.mw + 40 / step) * 4 + 1] != 4)
                throw std::runtime_error("A foreground starting from zero MV missed its first visibility window");
            ++checks;
            // Common temporal acceleration must not manufacture inter-surface motion.
            TemporalGhostFixture camera(f, 128, 64, variant != 0, variant == 2, step);
            camera.Frame(40, 12, .03f, 0, 0, false, reversed);
            auto state = camera.Frame(40, 12, .03f, 12, 12, false, reversed);
            for (size_t i = 1; i < state.size(); i += 4)
                if (state[i] != 0)
                    throw std::runtime_error("Common camera acceleration falsely created a visibility mask");
            ++checks;
            // A genuine event persists for N frames, then expires at a frozen edge without renewal.
            TemporalGhostFixture timer(f, 128, 64, variant != 0, variant == 2, step);
            timer.Frame(40, 4, .03f, 0, -4, false, reversed);
            state = timer.Frame(44, 4, .03f, 0, -4, false, reversed);
            for (unsigned frame = 1; frame <= 5; ++frame)
            {
                state = timer.Frame(44, 4, .03f, 0, 0, false, reversed);
                float expected = frame < 4 ? float(4 - frame) : 0;
                if (state[((24 / step) * timer.mw + 40 / step) * 4 + 1] != expected)
                    throw std::runtime_error("Frozen edge renewed or prematurely cancelled a real visibility timer");
                ++checks;
            }
        }
    std::cout << "Relative-layer visibility PASS: " << checks
              << " direction/speed/minimum/camera/frozen-expiry cases\n";
}
// Whole revealed regions, not just a few samples near a depth edge.
void WideVisibilityQuality(Fixture& f, bool baseline = false)
{
    unsigned checks = 0, missing = 0, revealed = 0;
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
            for (unsigned bits : { 0u, 16u, 24u })
                for (int speed : { -80, -40, 40, 80 })
                    for (bool starting : { false, true })
                    {
                        const unsigned step = variant == 3 ? 4 : 2;
                        TemporalGhostFixture t(f, 256, 64, variant != 0, variant == 2, step);
                        if (baseline)
                            t.age = f.r.pipeline("artifacts/ghost-study/occluded-interior-baseline",
                                                 variant == 3 ? "private_ghost_coarse_2"
                                                 : variant    ? "private_ghost_compact_2"
                                                              : "private_ghost_history");
                        t.Frame(80, 64, .03f, starting ? 0.f : float(speed), 0, false, reversed, false, bits);
                        std::vector<unsigned> expected(t.mw * t.mh, 0);
                        for (unsigned frame = 0; frame < 5; ++frame)
                        {
                            auto state = t.Frame(80, 64, .03f, float(speed), 0, false, reversed, false, bits);
                            auto previous = expected;
                            for (unsigned y = 0; y < t.mh; ++y)
                                for (unsigned x = 0; x < t.mw; ++x)
                                {
                                    const int px = int(x * step), py = int(y * step), oldX = px + speed;
                                    const bool currentObject = px >= 80 && px < 144 && py >= 16 && py < 48;
                                    const bool oldObject = oldX >= 80 && oldX < 144 && py >= 16 && py < 48;
                                    const size_t i = y * t.mw + x;
                                    expected[i] = 0;
                                    if (!currentObject && oldX >= 0 && oldX < 256)
                                    {
                                        if (oldObject)
                                            expected[i] = 4;
                                        else
                                            expected[i] = previous[y * t.mw + unsigned(oldX) / step] > 0
                                                              ? previous[y * t.mw + unsigned(oldX) / step] - 1
                                                              : 0;
                                    }
                                    const float age = state[i * 4 + 1];
                                    if (expected[i] > 0)
                                        ++revealed;
                                    if (age != expected[i])
                                    {
                                        ++missing;
                                        if (!baseline)
                                            throw std::runtime_error(
                                                "Wide disocclusion coverage/expiry failed variant=" +
                                                std::to_string(variant) + " starting=" + std::to_string(starting) +
                                                " speed=" + std::to_string(speed) + " frame=" + std::to_string(frame) +
                                                " x=" + std::to_string(px) + " y=" + std::to_string(py) + " age=" +
                                                std::to_string(age) + " expected=" + std::to_string(expected[i]));
                                    }
                                }
                        }
                        ++checks;
                    }
    if (baseline && !missing)
        throw std::runtime_error("Pinned interior-hole baseline did not reproduce missing timers");
    std::cout << "Whole revealed-area " << (baseline ? "baseline" : "PASS") << ": " << checks << " sequences, "
              << revealed << " active expected samples, " << missing << " incorrect timers\n";
}
void StaticVisibilityCapture(Fixture& f, const std::string& stem)
{
    unsigned w, h, mw, mh;
    auto z = GuidedPlane(stem + ".depth.dds", 1, 41, w, h), mv = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != mw || h != mh)
        throw std::runtime_error("Static capture guide dimensions");
    for (unsigned step : { 2u, 4u })
        for (bool zero : { false, true })
        {
            TemporalGhostFixture t(f, w, h, true, step == 2, step);
            const auto motion = zero ? std::vector<float>(mv.size(), 0) : mv;
            std::vector<unsigned> active;
            for (unsigned frame = 0; frame < 4; ++frame)
            {
                const auto state = t.FrameGuides(z, motion, false, true, 0, 19, 9, .05f, 2.02f);
                unsigned count = 0;
                for (size_t i = 1; i < state.size(); i += 4)
                    count += state[i] > 0;
                active.push_back(count);
            }
            std::cout << "Frozen capture " << std::filesystem::path(stem).filename().string() << " step=" << step
                      << " zeroMV=" << zero << " active timers";
            for (auto count : active)
                std::cout << ' ' << count;
            std::cout << '\n';
        }
}

// Diagnostic only: frozen nonzero guides approximate a mask, never replay recorded temporal state.
void FrozenVisibilityPreview(Fixture& f, const std::string& stem, unsigned frames, bool baseline = false,
                             bool skyBaseline = false, bool feather = false, unsigned ringLayers = 0,
                             float motionGate = 2.f)
{
    unsigned w, h, aw, ah, dw, dh, mw, mh;
    auto raw = GuidedPlane(stem + ".raw.dds", 4, 2, w, h), after = GuidedPlane(stem + ".after.dds", 4, 2, aw, ah),
         depth = GuidedPlane(stem + ".depth.dds", 1, 41, dw, dh),
         motion = GuidedPlane(stem + ".motion.dds", 2, 16, mw, mh);
    if (w != aw || h != ah || w != dw || h != dh || w != mw || h != mh || frames < 1 || frames > 120)
        throw std::runtime_error("Frozen preview dimensions/frame count");
    TemporalGhostFixture t(f, w, h, true, false, 4);
    if (skyBaseline)
    {
        t.scatter = f.r.pipeline("artifacts/ghost-study/sky-baseline", "private_ghost_coarse_1");
        t.age = f.r.pipeline("artifacts/ghost-study/sky-baseline", "private_ghost_coarse_2");
    }
    if (baseline)
        t.age = f.r.pipeline("artifacts/ghost-study/road-baseline", "private_ghost_coarse_2");
    for (unsigned frame = 0; frame < frames; ++frame)
        t.FrameGuides(depth, motion, false, true, 0, 18, 12, .05f, motionGate);
    const std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    f.r.begin();
    std::vector<float> zero(raw.size(), 0);
    for (size_t i = 0; i < after.size(); i += 4)
        for (unsigned ch = 0; ch < 3; ++ch)
            after[i + ch] += .5f;
    auto r = f.texture(w, h, &raw, DXGI_FORMAT_R16G16B16A16_FLOAT),
         a = f.texture(w, h, &after, DXGI_FORMAT_R16G16B16A16_FLOAT),
         p = f.texture(w, h, &zero, DXGI_FORMAT_R16G16B16A16_FLOAT), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
         field = f.texture(t.mw, t.mh, nullptr, DXGI_FORMAT_R16_FLOAT), out = f.texture(w, h);
    DlssNr::GhostRejection::Settings settings { true, .01f, .02f, 1, false, 5 };
    settings.historyFrames = 18;
    settings.historyFade = 12;
    settings.outwardMethod = ringLayers ? 1u : 0u;
    settings.outwardLayers = ringLayers ? ringLayers : 1u;
    settings.motionGate = motionGate;
    DlssNr::Superior::DlssNrConstants c {};
    DlssNr::Superior::Fields::SetCoarseGhostDilate(c, t.mw, t.mh, settings);
    auto dilate = f.r.pipeline(dir, "private_ghost_coarse_dilate_1");
    f.dispatch(dilate.Get(), c, t.history[t.index].Get(), p.Get(), field.Get(), (t.mw + 7) / 8, (t.mh + 7) / 8);
    f.r.barrier(field.Get(), UAV, SRV);
    c = {};
    c.Mode = 38;
    c.Width = w;
    c.Height = h;
    DlssNr::Superior::Fields::SetPositiveCarrierGain(c, 1);
    DlssNr::Superior::Fields::SetCarrierDepthConvention(c, true);
    DlssNr::Superior::Fields::SetCarrierGhostRejection(c, settings);
    auto decode = f.r.pipeline(skyBaseline ? "artifacts/ghost-study/sky-baseline" : dir, "private_carrier_coarse");
    f.dispatch(decode.Get(), c, a.Get(), p.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(), r.Get(),
               field.Get());
    if (feather)
    {
        const unsigned step = ringLayers
                                  ? 4u
                                  : std::clamp(unsigned(std::ceil(settings.outwardRadius * h / 1080.f)), 1u, 128u),
                       fw = (w + step - 1) / step, fh = (h + step - 1) / step;
        auto horizontal = f.texture(fw, fh, nullptr, DXGI_FORMAT_R32G32_FLOAT), expanded = f.texture(w, h);
        auto hp = f.visibilityPipeline(ringLayers ? "private_ghost_feather_rings" : "private_ghost_feather_horizontal"),
             vp =
                 f.visibilityPipeline(ringLayers ? "private_ghost_feather_rings_apply" : "private_ghost_feather_apply");
        f.r.barrier(out.Get(), UAV, SRV);
        DlssNr::Superior::Fields::SetGhostOutwardFeather(c, w, h, settings, true, 0, 0, false);
        f.dispatch(hp.Get(), c, out.Get(), out.Get(), horizontal.Get(), (fw + 7) / 8, (fh + 7) / 8, d.Get());
        f.r.barrier(horizontal.Get(), UAV, SRV);
        DlssNr::Superior::Fields::SetGhostOutwardFeather(c, w, h, settings, true, 0, 0, true);
        f.dispatch(vp.Get(), c, out.Get(), horizontal.Get(), expanded.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(),
                   r.Get());
        out = expanded;
    }
    auto values = f.read(out.Get());
    const auto name = std::filesystem::path(stem).filename().string();
    std::ofstream output("artifacts/ghost-study/" + name +
                             (ringLayers    ? ".rings" + std::to_string(ringLayers) + "-mask-"
                              : skyBaseline ? ".sky-baseline-"
                              : feather     ? ".feather-mask-"
                              : baseline    ? ".frozen-mask-baseline-"
                                            : ".frozen-mask-") +
                             std::to_string(frames) + ".dds",
                         std::ios::binary);
    if (!DlssNr::EditDump::WriteRows(w, h, values.data(), size_t(w) * 16,
                                     [&](const void* data, size_t bytes)
                                     {
                                         output.write(static_cast<const char*>(data), bytes);
                                         return bool(output);
                                     }))
        throw std::runtime_error("Frozen preview save failed");
    std::cout << "Frozen-guide approximation saved: " << name << " frames=" << frames
              << " (not recorded game history)\n";
}

void StaticVisibilityTiming(Fixture& f, const std::string& stem, bool moving = false, bool interior = false,
                            bool contrast = false, bool road = false, bool brightness = false, bool sky = false,
                            bool feather = false, bool fastCompare = false, unsigned ringLayers = 0, bool pitch = false,
                            bool outputGate = false, const std::string& outputGateBaseline = {})
{
    const bool compareOutputGate = !outputGateBaseline.empty();
    const unsigned radius = 1;
    const bool residual = true;
    const float gMathGain = 1, gMathBlend = 0;
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
    ComPtr<ID3D12Resource> movingDepth[2], movingMotion[2];
    if (moving)
        for (unsigned phase = 0; phase < 2; ++phase)
        {
            std::vector<float> z(w * h * 4, 0), mv(z);
            const unsigned left = (w / 3 + 3) & ~3u;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const size_t i = (size_t(y) * w + x) * 4;
                    bool object = x >= left + (interior ? 0 : phase * 8) &&
                                  x < left + (interior ? 0 : phase * 8) + w / 12 && y >= h / 6 && y < h * 5 / 6;
                    z[i] = object ? .03f : .01f;
                    mv[i] = interior ? (object ? 0.f : (phase ? -128.f : 128.f)) : object ? (phase ? -8.f : 8.f) : 0.f;
                    if (road)
                    {
                        // Constant forward camera over an infinite plane; foreground moves with the camera.
                        object = x >= w / 4 && x < w * 7 / 20 && y >= h / 2 && y < h * 9 / 10;
                        const float roadDepth =
                            std::max(.0001f, .046365f * (float(y) + .5f - float(h) * .314f) / float(h));
                        z[i] = object ? .045f : roadDepth;
                        const float k = 1.f / (1 + 2.5f * roadDepth);
                        mv[i] = object ? 0.f : (float(x) + .5f - float(w) * .52f) * (k - 1);
                        mv[i + 1] = object ? 0.f : (float(y) + .5f - float(h) * .314f) * (k - 1);
                    }
                    if (pitch)
                    {
                        // Analytic plane with alternating camera pitch, independent of the mask algorithm.
                        const double angle = phase ? .15 : .175, oldAngle = phase ? .175 : .15;
                        const double delta = angle - oldAngle, focal = double(h) * .75;
                        const double sy = (double(y) + .5 - h * .5) / focal, sx = (double(x) + .5 - w * .5) / focal;
                        const double roadDepth = std::max(.00001, .015 * (std::sin(angle) + sy * std::cos(angle)));
                        const double den = std::cos(delta) - sy * std::sin(delta) + 8 * roadDepth * std::cos(oldAngle);
                        const double px = sx / den, py = (std::sin(delta) + sy * std::cos(delta) -
                                                          8 * roadDepth * std::sin(oldAngle)) /
                                                         den;
                        object = x >= w * 3 / 8 && x < w * 49 / 80 && y >= h * 4 / 15 && y < h * 13 / 20;
                        z[i] = object ? .06f : float(roadDepth);
                        mv[i] = object ? 0.f : float((px - sx) * focal);
                        mv[i + 1] = object ? 0.f : float((py - sy) * focal);
                    }
                }
            movingDepth[phase] = f.texture(w, h, &z, DXGI_FORMAT_R32_FLOAT);
            movingMotion[phase] = f.texture(w, h, &mv, DXGI_FORMAT_R32G32_FLOAT);
        }
    ComPtr<ID3D12Resource> outputs[2] = { f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT),
                                          f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT) };
    struct State
    {
        ComPtr<ID3D12Resource> recon, state[2], field, raw[2], reference[2], expanded, featherHorizontal, featherOutput;
        unsigned index = 0, frame = 0;
        bool valid = false;
    };
    State states[2];
    for (unsigned k = 0; k < 2; ++k)
    {
        auto& s = states[k];
        const unsigned stride = 4;
        const unsigned mw = (w + stride - 1) / stride, mh = (h + stride - 1) / stride;
        s.recon = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32_UINT);
        s.field = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16_FLOAT);
        for (auto& hist : s.state)
            hist = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32G32_UINT);
        if (feather && (k == 1 || fastCompare))
        {
            const unsigned step = ringLayers ? 4u : std::clamp(unsigned(std::ceil(4.f * h / 1080.f)), 1u, 128u);
            s.featherHorizontal =
                k == 0 ? f.texture(w, h, nullptr, DXGI_FORMAT_R16_FLOAT)
                       : f.texture((w + step - 1) / step, (h + step - 1) / step, nullptr, DXGI_FORMAT_R32G32_FLOAT);
            s.featherOutput = f.texture(w, h);
        }
        if (contrast && (k == 1 || brightness || feather))
        {
            for (auto& t : s.raw)
                t = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
            for (auto& t : s.reference)
                t = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
            s.expanded = f.texture(mw, mh, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
    }
    f.r.finish();
    f.uploads.clear();
    std::string dir = "OptiScaler/shaders/dlssnr/superior/precompile";
    const std::string oldDir = pitch                   ? "artifacts/ghost-study/pitch-baseline"
                               : sky                   ? "artifacts/ghost-study/sky-baseline"
                               : (contrast || feather) ? dir
                               : road                  ? "artifacts/ghost-study/road-baseline"
                               : interior              ? "artifacts/ghost-study/occluded-interior-baseline"
                                                       : "artifacts/ghost-study/static-baseline";
    auto clear = f.r.pipeline(dir, "private_ghost_clear"),
         baseScatter = f.r.pipeline(sky ? oldDir : dir, "private_ghost_coarse_1"),
         baseAge = f.r.pipeline(oldDir, "private_ghost_coarse_2"),
         baseDecode = f.r.pipeline(sky ? oldDir : dir, "private_carrier_coarse"),
         scatter = f.r.pipeline(dir, "private_ghost_coarse_1"), age = f.r.pipeline(dir, "private_ghost_coarse_2"),
         decode16 = f.r.pipeline(dir, "private_carrier_coarse");
    auto dilate = f.r.pipeline(dir, "private_ghost_coarse_dilate_1");
    auto featherH =
        feather ? f.r.pipeline(dir, ringLayers ? "private_ghost_feather_rings" : "private_ghost_feather_horizontal")
                : ComPtr<ID3D12PipelineState> {};
    auto featherApply = feather ? f.r.pipeline(dir, ringLayers ? "private_ghost_feather_rings_inplace"
                                                               : "private_ghost_feather_inplace")
                                : ComPtr<ID3D12PipelineState> {};
    auto oldFeatherH =
        fastCompare ? f.r.pipeline("artifacts/ghost-study/feather-full-baseline", "private_ghost_feather_horizontal")
                    : ComPtr<ID3D12PipelineState> {};
    auto oldFeatherApply =
        fastCompare ? f.r.pipeline("artifacts/ghost-study/feather-full-baseline", "private_ghost_feather_apply")
                    : ComPtr<ID3D12PipelineState> {};
    auto contrastHistory =
        contrast ? f.r.pipeline(dir, "private_ghost_contrast_coarse") : ComPtr<ID3D12PipelineState> {};
    auto contrastDilate =
        contrast ? f.r.pipeline(dir, "private_ghost_contrast_dilate_1") : ComPtr<ID3D12PipelineState> {};
    auto contrastDecode =
        contrast ? f.r.pipeline(dir, "private_carrier_contrast_coarse_0") : ComPtr<ID3D12PipelineState> {};
    auto baselineContrastDecode =
        compareOutputGate ? f.r.pipeline(outputGateBaseline, "private_carrier_contrast_coarse_0") : contrastDecode;
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
        const unsigned stride = 4;
        const unsigned mw = (w + stride - 1) / stride, mh = (h + stride - 1) / stride;
        unsigned next = s.valid ? 1 - s.index : 0;
        auto* guideDepth = moving ? movingDepth[s.frame & 1].Get() : d.Get();
        auto* guideMotion = moving ? movingMotion[s.frame & 1].Get() : m.Get();
        ++s.frame;
        f.r.begin();
        if (s.valid)
            f.r.barrier(out.Get(), SRV, UAV);
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (s.valid)
            f.r.barrier(s.recon.Get(), SRV, UAV);
        if (s.valid || next == 1)
            f.r.barrier(s.state[next].Get(), SRV, UAV);
        DlssNr::Superior::DlssNrConstants c {};
        DlssNr::GhostRejection::Settings settings { true, .01f, .02f, 1, false, 2 };
        settings.historyFrames = road ? 18 : 19;
        settings.historyFade = road ? 12 : 9;
        settings.motionGate = pitch ? 2.5f : 2.02f;
        DlssNr::Superior::Fields::SetGhostVisibility(c, mw, mh, w, h, settings, true, s.valid, 0);

        if (!s.valid)
            f.r.barrier(s.state[1].Get(), UAV, SRV);
        auto* previous = s.valid ? s.state[s.index].Get() : s.state[1].Get();
        c.Mode = 44;
        f.dispatch(clear.Get(), c, guideDepth, guideMotion, s.recon.Get(), (mw + 7) / 8, (mh + 7) / 8, nullptr,
                   previous, nullptr, nullptr, nullptr, 0, false);
        const auto uavBarrier = [&](ID3D12Resource* resource)
        {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            f.r.list->ResourceBarrier(1, &barrier);
        };
        uavBarrier(s.recon.Get());
        c.Mode = 45;
        f.dispatch(variant ? scatter.Get() : baseScatter.Get(), c, guideDepth, guideMotion, s.recon.Get(), (mw + 7) / 8,
                   (mh + 7) / 8, nullptr, previous, nullptr, nullptr, nullptr, 1, false, s.state[next].Get());
        f.r.barrier(s.recon.Get(), UAV, SRV);
        uavBarrier(s.state[next].Get());
        c.Mode = 46;
        f.dispatch(variant ? age.Get() : baseAge.Get(), c, guideDepth, guideMotion, s.state[next].Get(), (mw + 7) / 8,
                   (mh + 7) / 8, s.recon.Get(), previous, nullptr, nullptr, nullptr, 2, false);
        f.r.barrier(s.state[next].Get(), UAV, SRV);
        settings.method = 5;
        settings.outwardMethod = ringLayers ? 1u : 0u;
        settings.outwardLayers = ringLayers ? ringLayers : 1u;
        settings.contrastEnabled = contrast && (!outputGate || brightness) && (variant == 1 || brightness || feather);
        settings.outputGate = outputGate && (variant == 1 || compareOutputGate);
        if (compareOutputGate)
        {
            settings.outputThreshold = .101f;
            settings.contrastThreshold = .0723f;
            settings.contrastSoftness = .01f;
        }
        const bool referenceEnabled = DlssNr::GhostRejection::NeedsEditReference(settings);
        settings.contrastMetric = brightness && (variant == 1 || outputGate) ? 1u : 0u;
        if (referenceEnabled)
        {
            if (s.valid)
            {
                f.r.barrier(s.raw[next].Get(), SRV, UAV);
                f.r.barrier(s.reference[next].Get(), SRV, UAV);
            }
            else
            {
                f.r.barrier(s.raw[1].Get(), UAV, SRV);
                f.r.barrier(s.reference[1].Get(), UAV, SRV);
            }
            DlssNr::Superior::Fields::SetGhostContrastHistory(c, 5, s.valid);
            auto* oldRaw = s.valid ? s.raw[s.index].Get() : p.Get();
            auto* oldRef = s.valid ? s.reference[s.index].Get() : p.Get();
            f.dispatch(contrastHistory.Get(), c, s.state[next].Get(), oldRaw, s.raw[next].Get(), (mw + 7) / 8,
                       (mh + 7) / 8, p.Get(), oldRef, r.Get(), guideDepth, nullptr, 3, false, s.reference[next].Get());
            f.r.barrier(s.raw[next].Get(), UAV, SRV);
            f.r.barrier(s.reference[next].Get(), UAV, SRV);
        }
        ID3D12Resource* decodeGuide = s.state[next].Get();
        if (s.field)
        {
            if (s.valid)
                f.r.barrier(s.field.Get(), SRV, UAV);
            DlssNr::Superior::Fields::SetCoarseGhostDilate(c, mw, mh, settings);
            if (referenceEnabled)
            {
                c.Mode = 53;
                if (s.valid)
                    f.r.barrier(s.expanded.Get(), SRV, UAV);
            }
            f.dispatch(referenceEnabled ? contrastDilate.Get() : dilate.Get(), c, s.state[next].Get(),
                       referenceEnabled ? s.reference[next].Get() : s.state[next].Get(), s.field.Get(), (mw + 7) / 8,
                       (mh + 7) / 8, nullptr, nullptr, nullptr, nullptr, nullptr, contrast ? 4 : 3, false,
                       referenceEnabled ? s.expanded.Get() : nullptr);
            if (referenceEnabled)
                f.r.barrier(s.expanded.Get(), UAV, SRV);
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
        f.dispatch(referenceEnabled ? (variant ? contrastDecode.Get() : baselineContrastDecode.Get())
                   : variant        ? decode16.Get()
                                    : baseDecode.Get(),
                   dc, a.Get(), referenceEnabled ? s.expanded.Get() : p.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8,
                   guideDepth, p.Get(), r.Get(), decodeGuide, nullptr, contrast ? 5 : 4, false);
        f.r.barrier(out.Get(), UAV, SRV);
        if (feather && (variant == 1 || fastCompare))
        {
            if (s.valid)
            {
                f.r.barrier(s.featherHorizontal.Get(), SRV, UAV);
                if (variant == 0)
                    f.r.barrier(s.featherOutput.Get(), SRV, UAV);
            }
            DlssNr::Superior::Fields::SetGhostOutwardFeather(c, w, h, settings, true, 0, 0, false);
            if (variant == 0)
            {
                c.Width = w;
                c.Height = h;
                f.dispatch(oldFeatherH.Get(), c, out.Get(), out.Get(), s.featherHorizontal.Get(), (w + 7) / 8,
                           (h + 7) / 8, guideDepth, nullptr, nullptr, nullptr, nullptr, 6, false);
                f.r.barrier(s.featherHorizontal.Get(), UAV, SRV);
                c.Mode = 55;
                f.dispatch(oldFeatherApply.Get(), c, out.Get(), s.featherHorizontal.Get(), s.featherOutput.Get(),
                           (w + 7) / 8, (h + 7) / 8, guideDepth, p.Get(), r.Get(), nullptr, nullptr, 7, false);
            }
            else
            {
                const unsigned fw = c.Width, fh = c.Height;
                f.dispatch(featherH.Get(), c, out.Get(), out.Get(), s.featherHorizontal.Get(), (fw + 7) / 8,
                           (fh + 7) / 8, guideDepth, nullptr, nullptr, nullptr, nullptr, 6, false);
                f.r.barrier(s.featherHorizontal.Get(), UAV, SRV);
                DlssNr::Superior::Fields::SetGhostOutwardFeather(c, w, h, settings, true, 0, 0, true);
                c.Mode = ringLayers ? 59u : 56u;
                f.r.barrier(out.Get(), SRV, UAV);
                f.dispatch(featherApply.Get(), c, p.Get(), s.featherHorizontal.Get(), out.Get(), (w + 7) / 8,
                           (h + 7) / 8, guideDepth, p.Get(), r.Get(), nullptr, nullptr, 7, false);
                f.r.barrier(out.Get(), UAV, SRV);
            }
            if (variant == 0)
                f.r.barrier(s.featherOutput.Get(), UAV, SRV);
        }
        f.r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        f.r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
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
    const auto prefix = "artifacts/ghost-study/" + name +
                        (compareOutputGate ? ".output-range"
                         : outputGate      ? (brightness ? ".output-gate-with-contrast" : ".output-gate")
                         : pitch           ? ".pitch"
                         : ringLayers      ? ".rings" + std::to_string(ringLayers)
                         : fastCompare     ? ".feather-fast"
                         : feather         ? ".feather"
                         : sky             ? ".sky"
                         : brightness      ? ".brightness"
                         : road            ? ".road"
                         : contrast        ? ".contrast"
                         : interior        ? ".interior"
                                           : "") +
                        (moving ? ".moving-fix" : ".static-fix");
    std::ofstream csv(prefix + "-timing.csv");
    csv << "sample,width,height,comparison,block,position,variant,ms\n";
    const unsigned blocks = 64;
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
    {
        const auto readSrv = [&](ID3D12Resource* resource)
        {
            f.r.begin();
            f.r.barrier(resource, SRV, UAV);
            return f.read(resource);
        };
        auto oldOutput = readSrv(fastCompare ? states[0].featherOutput.Get() : outputs[0].Get()),
             newOutput = readSrv(outputs[1].Get());
        double sum = 0, sumSquared = 0;
        float maxRGB = 0, maxMask = 0;
        size_t changed = 0, increased = 0, decreased = 0, strongLost = 0, soft = 0, oldActive = 0, newActive = 0;
        for (size_t i = 0; i < oldOutput.size(); ++i)
        {
            if (!std::isfinite(newOutput[i]))
                throw std::runtime_error("Nonfinite coarse output");
            const float delta = std::abs(newOutput[i] - oldOutput[i]);
            if (i % 4 == 3)
            {
                if (newOutput[i] < 0 || newOutput[i] > 1)
                    throw std::runtime_error("Invalid coarse weight");
                if (((contrast && !brightness && !feather) || (outputGate && !brightness)) &&
                    newOutput[i] > oldOutput[i] + .002f)
                    throw std::runtime_error("Contrast gate increased the visibility mask");
                if (feather && !fastCompare && newOutput[i] < oldOutput[i])
                    throw std::runtime_error("Outward feather eroded the post-contrast mask");
                if (brightness && !outputGate && newOutput[i] < oldOutput[i] - .002f)
                    throw std::runtime_error("Brightness metric removed a mask retained by maximum RGB");
                maxMask = std::max(maxMask, delta);
                oldActive += oldOutput[i] > 0;
                newActive += newOutput[i] > 0;
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
                << ",\n  \"old_active_mask_pixels\": " << oldActive << ",\n  \"new_active_mask_pixels\": " << newActive
                << ",\n  \"history_bit_exact\": false\n}\n";
        std::cout << "Static fix quality: increased " << increased << ", decreased " << decreased << ", strong loss "
                  << strongLost << " / " << pixels << " pixels\n";
    }
    std::cout << "Static fix whole-region GPU ABBA saved for " << name << '\n';
}
