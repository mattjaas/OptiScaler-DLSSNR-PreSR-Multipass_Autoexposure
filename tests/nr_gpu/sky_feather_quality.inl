// Visibility expectations include a real far plane, which is valid sky rather than missing depth.
void SkyVisibilityQuality(Fixture& f)
{
    unsigned checks = 0;
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
            for (unsigned bits : { 0u, 16u, 24u })
            {
                const unsigned w = 128, h = 64, step = variant == 3 ? 4 : 2;
                TemporalGhostFixture t(f, w, h, variant != 0, variant == 2, step);
                auto guides = [&](int left, float speed, bool invalid = false)
                {
                    std::vector<float> z(w * h * 4, 0), mv(z);
                    for (unsigned y = 0; y < h; ++y)
                        for (unsigned x = 0; x < w; ++x)
                        {
                            size_t i = (y * w + x) * 4;
                            bool object = int(x) >= left && int(x) < left + 8 && y >= 16 && y < 48;
                            float n = object ? .03f : 0;
                            float depth = reversed ? n : 1 - n;
                            if (bits)
                                depth = std::round(depth * ((1u << bits) - 1)) / float((1u << bits) - 1);
                            z[i] = invalid ? NAN : depth;
                            mv[i] = object ? speed : 0;
                        }
                    return t.FrameGuides(z, mv, false, reversed, bits);
                };
                guides(40, 0);
                auto stationary = guides(40, 0);
                for (size_t i = 1; i < stationary.size(); i += 4)
                    if (stationary[i] != 0)
                        throw std::runtime_error("Static sky edge seeded a timer");
                auto moved = guides(44, -4);
                size_t revealed = ((24 / step) * t.mw + 40 / step) * 4;
                if (moved[revealed] <= 0 || moved[revealed + 1] != 4)
                    throw std::runtime_error("Revealed sky missing mask variant=" + std::to_string(variant));
                size_t foreground = ((24 / step) * t.mw + 48 / step) * 4;
                if (moved[foreground + 1] != 0)
                    throw std::runtime_error("Sky mask covered new foreground");
                auto invalid = guides(48, 0, true);
                for (float v : invalid)
                    if (v != 0)
                        throw std::runtime_error("Invalid sky guides not cleared");
                ++checks;
            }
    std::cout << "Far-plane sky visibility PASS: " << checks
              << " moving/static/invalid sequences, all temporal methods, Z/D16/D24\n";
}

// Independent scalar post-contrast reference, including one-sided depth and upward FP16 rounding.
void OutwardFeatherQuality(Fixture& f)
{
    using namespace DlssNr::Superior;
    unsigned checks = 0;
    auto hp = f.visibilityPipeline("private_ghost_feather_horizontal"),
         vp = f.visibilityPipeline("private_ghost_feather_apply"),
         ip = f.visibilityPipeline("private_ghost_feather_inplace");
    auto up = [](float v)
    {
        auto bits = DirectX::PackedVector::XMConvertFloatToHalf(v);
        if (Half(bits) < v)
            ++bits;
        return Half(bits);
    };
    for (bool inplace : { false, true })
        for (bool reversed : { false, true })
            for (float global : { 0.f, .25f, .875f })
                for (float radius : { 4.f, 16.f, 32.f, NAN })
                    for (unsigned scenario : { 0u, 1u, 2u, 3u, 4u, 5u })
                    {
                        const unsigned w = 33, h = 109;
                        std::vector<float> base(w * h * 4, 0), proxy(base), answer(base), depth(base);
                        const float raw[3] = { .0625f, -.03125f, 2.f }, filtered[3] = { .125f, -.125f, 4.f };
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = (y * w + x) * 4;
                                float nearZ = scenario == 2 ? 0 : .01f;
                                if (scenario != 0 && x >= w / 2)
                                    nearZ = .03f;
                                if (scenario == 4)
                                    nearZ = x < w / 2 ? .03f : .01f;
                                if (scenario == 5)
                                    nearZ = y < h / 2 ? .01f : .03f;
                                depth[i] = reversed ? nearZ : 1 - nearZ;
                                if (scenario == 3 && x == w / 2)
                                    depth[i] = NAN;
                                // Final post-contrast mask: the right half has been cancelled. Fill only a soft fringe.
                                float mask = (x == w / 2 - 2 && y >= h / 3 && y < 2 * h / 3) ? 1.f : 0.f;
                                if (x == 0 && y == 0)
                                    mask = .5f; // image border
                                base[i + 3] = (1 - global) * mask;
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    answer[i + c] = raw[c];
                                    base[i + c] =
                                        filtered[c] * (1 - global - base[i + 3]) + raw[c] * (global + base[i + 3]);
                                }
                            }
                        auto expected = base;
                        const unsigned step =
                            std::isfinite(radius) ? std::clamp(unsigned(std::ceil(radius * h / 1080.f)), 1u, 128u) : 1u;
                        const unsigned mw = (w + step - 1) / step, mh = (h + step - 1) / step;
                        struct Sample
                        {
                            float mask = 0, depth = 0;
                        };
                        std::vector<Sample> blocks(mw * mh);
                        auto nearDepth = [&](size_t pixel)
                        {
                            double z = depth[pixel * 4];
                            return std::isfinite(z) && z >= 0 && z <= 1 ? std::max(reversed ? z : 1 - z, 1e-20) : -1.;
                        };
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = y * w + x;
                                auto& block = blocks[(y / step) * mw + x / step];
                                if (base[i * 4 + 3] > block.mask)
                                    block = { base[i * 4 + 3], float(nearDepth(i)) };
                            }
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = y * w + x;
                                float a = base[i * 4 + 3];
                                double z = nearDepth(i);
                                if (step > 1 && std::isfinite(radius) && z > 0)
                                {
                                    double px = (x + .5) / step - .5, py = (y + .5) / step - .5;
                                    int bx = int(std::floor(px)), by = int(std::floor(py));
                                    double fx = px - bx, fy = py - by, sum = 0;
                                    for (int dy = 0; dy < 2; ++dy)
                                        for (int dx = 0; dx < 2; ++dx)
                                        {
                                            auto q = blocks[std::clamp(by + dy, 0, int(mh) - 1) * mw +
                                                            std::clamp(bx + dx, 0, int(mw) - 1)];
                                            if (q.depth > 0 && z <= q.depth * 1.05 + 1e-6)
                                                sum += q.mask * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                                        }
                                    a = std::max(a, up(float(sum)));
                                }
                                a = std::max(base[i * 4 + 3], std::min(a, 1 - global));
                                expected[i * 4 + 3] = a;
                                for (unsigned c = 0; c < 3; ++c)
                                    expected[i * 4 + c] = filtered[c] * (1 - global - a) + raw[c] * (global + a);
                            }
                        f.r.begin();
                        auto b = f.texture(w, h, &base, DXGI_FORMAT_R16G16B16A16_FLOAT), p = f.texture(w, h, &proxy),
                             a = f.texture(w, h, &answer), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                             field = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32G32_FLOAT),
                             out = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
                        DlssNr::GhostRejection::Settings settings;
                        settings.outwardRadius = std::isfinite(radius) ? radius : 4;
                        DlssNr::Superior::DlssNrConstants c {};
                        Fields::SetGhostOutwardFeather(c, w, h, settings, reversed, global, 0, false);
                        if (!std::isfinite(radius))
                        {
                            c.GuideWidth = 1;
                            c.Width = mw;
                            c.Height = mh;
                        }
                        c.WhitePoint = radius;
                        f.dispatch(hp.Get(), c, b.Get(), b.Get(), field.Get(), (mw + 7) / 8, (mh + 7) / 8, d.Get());
                        f.r.barrier(field.Get(), UAV, SRV);
                        Fields::SetGhostOutwardFeather(c, w, h, settings, reversed, global, 0, true);
                        if (!std::isfinite(radius))
                            c.GuideWidth = 1;
                        c.WhitePoint = radius;
                        if (inplace)
                        {
                            c.Mode = 56;
                            f.r.barrier(b.Get(), SRV, UAV);
                            f.dispatch(ip.Get(), c, p.Get(), field.Get(), b.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(),
                                       p.Get(), a.Get());
                            out = b;
                        }
                        else
                            f.dispatch(vp.Get(), c, b.Get(), field.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(),
                                       p.Get(), a.Get());
                        auto got = f.read(out.Get());
                        for (size_t i = 0; i < got.size(); ++i)
                        {
                            Near(got[i], expected[i], "outward feather independent RGB/alpha/depth");
                            if (i % 4 == 3 && got[i] < base[i])
                                throw std::runtime_error("Feather eroded mask core");
                        }
                        ++checks;
                    }
    std::cout << "Outward feather PASS: " << checks
              << " independent full-image cases, Z/sky/occluders/NaN/HDR/borders/global fade\n";
}
