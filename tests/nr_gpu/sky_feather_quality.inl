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
         vp = f.visibilityPipeline("private_ghost_feather_apply");
    auto up = [](float v)
    {
        auto bits = DirectX::PackedVector::XMConvertFloatToHalf(v);
        if (Half(bits) < v)
            ++bits;
        return Half(bits);
    };
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
                    std::vector<float> horizontal(w * h, 0);
                    const double r = std::isfinite(radius) ? double(radius) * h / 1080 : 0;
                    auto nearDepth = [&](size_t pixel)
                    {
                        double z = depth[pixel * 4];
                        return std::isfinite(z) && z >= 0 && z <= 1 ? std::max(reversed ? z : 1 - z, 1e-20) : -1.;
                    };
                    for (unsigned pass = 0; pass < 2; ++pass)
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t pixel = y * w + x;
                                float a = base[pixel * 4 + 3];
                                double z = nearDepth(pixel);
                                if (r > 0 && z > 0)
                                    for (int o = -int(std::ceil(r)); o <= int(std::ceil(r)); ++o)
                                    {
                                        int xx = int(x) + (pass ? 0 : o), yy = int(y) + (pass ? o : 0);
                                        if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h))
                                            continue;
                                        size_t q = size_t(yy) * w + xx;
                                        double nz = nearDepth(q);
                                        if (nz <= 0 || z > nz * 1.05 + 1e-6)
                                            continue;
                                        double value = (pass ? horizontal[q] : base[q * 4 + 3]) *
                                                       std::max(0., 1 - std::abs(o) / (r + 1));
                                        a = std::max(a, float(value));
                                    }
                                a = std::max(base[pixel * 4 + 3], up(a));
                                if (!pass)
                                    horizontal[pixel] = a;
                                else
                                {
                                    a = std::max(base[pixel * 4 + 3], std::min(a, 1 - global));
                                    expected[pixel * 4 + 3] = a;
                                    for (unsigned c = 0; c < 3; ++c)
                                        expected[pixel * 4 + c] =
                                            filtered[c] * (1 - global - a) + raw[c] * (global + a);
                                }
                            }
                    f.r.begin();
                    auto b = f.texture(w, h, &base, DXGI_FORMAT_R16G16B16A16_FLOAT), p = f.texture(w, h, &proxy),
                         a = f.texture(w, h, &answer), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                         field = f.texture(w, h, nullptr, DXGI_FORMAT_R16_FLOAT),
                         out = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
                    DlssNr::GhostRejection::Settings settings;
                    DlssNr::Superior::DlssNrConstants c {};
                    Fields::SetGhostOutwardFeather(c, w, h, settings, reversed, global, 0, false);
                    c.WhitePoint = radius;
                    f.dispatch(hp.Get(), c, b.Get(), b.Get(), field.Get(), (w + 7) / 8, (h + 7) / 8, d.Get());
                    f.r.barrier(field.Get(), UAV, SRV);
                    c.Mode = 55;
                    f.dispatch(vp.Get(), c, b.Get(), field.Get(), out.Get(), (w + 7) / 8, (h + 7) / 8, d.Get(), p.Get(),
                               a.Get());
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
