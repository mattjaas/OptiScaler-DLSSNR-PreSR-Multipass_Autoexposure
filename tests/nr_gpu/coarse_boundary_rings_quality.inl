// Independent Chebyshev-distance reference for 1-3 coarse boundary rings.
void CoarseBoundaryRingsQuality(Fixture& f)
{
    using namespace DlssNr::Superior;
    auto hp = f.visibilityPipeline("private_ghost_feather_rings"),
         vp = f.visibilityPipeline("private_ghost_feather_rings_apply"),
         ip = f.visibilityPipeline("private_ghost_feather_rings_inplace");
    auto up = [](float v)
    {
        auto b = DirectX::PackedVector::XMConvertFloatToHalf(v);
        if (Half(b) < v)
            ++b;
        return Half(b);
    };
    unsigned checks = 0;
    for (unsigned n : { 1u, 2u, 3u })
        for (bool inplace : { false, true })
            for (bool reversed : { false, true })
                for (float global : { 0.f, .25f, .875f })
                    for (unsigned shape : { 0u, 1u, 2u, 3u, 4u, 5u, 6u })
                    {
                        const unsigned w = 65, h = 41, mw = (w + 3) / 4, mh = (h + 3) / 4;
                        std::vector<float> base(w * h * 4, 0), proxy(base), answer(base), depth(base),
                            original(mw * mh, 0), seedDepth(mw * mh, 0);
                        const float raw[3] = { .0625f, -.03125f, 2.f }, filtered[3] = { .125f, -.125f, 4.f };
                        for (unsigned cy = 0; cy < mh; ++cy)
                            for (unsigned cx = 0; cx < mw; ++cx)
                            {
                                bool seed = shape == 6 ? (cx >= 3 && cx <= 10 && cy >= 2 && cy <= 8)
                                            : shape == 1
                                                ? (cx >= 3 && cx <= 7 && cy >= 2 && cy <= 6 && !(cx == 5 && cy == 4))
                                            : shape == 2 || shape == 4 ? cx == 7 && cy == 5
                                            : shape == 3               ? cx == 9 && cy == 5
                                            : shape == 5               ? cx == mw - 1 && cy == mh - 1
                                                                       : cx == 8 && cy == 5;
                                original[cy * mw + cx] = seed ? (1 - global) : 0;
                                if (shape == 6 && (cx == 3 || cx == 10 || cy == 2 || cy == 8))
                                    original[cy * mw + cx] *= .25f;
                                seedDepth[cy * mw + cx] = (shape == 3 ? .03f : .01f);
                                if (shape == 4)
                                    seedDepth[cy * mw + cx] = -1;
                            }
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = (y * w + x) * 4;
                                float z = (shape == 2 || shape == 3 || shape == 4) && x >= 32 ? .03f : .01f;
                                if (shape == 4 && x / 4 == 7 && y / 4 == 5)
                                    depth[i] = NAN;
                                else
                                    depth[i] = reversed ? z : 1 - z;
                                base[i + 3] = original[(y / 4) * mw + x / 4];
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    answer[i + c] = raw[c];
                                    base[i + c] =
                                        filtered[c] * (1 - global - base[i + 3]) + raw[c] * (global + base[i + 3]);
                                }
                            }
                        std::vector<bool> boundary(mw * mh, false);
                        for (unsigned y = 0; y < mh; ++y)
                            for (unsigned x = 0; x < mw; ++x)
                                if (original[y * mw + x] > 0)
                                    for (int dy = -1; dy <= 1; ++dy)
                                        for (int dx = -1; dx <= 1; ++dx)
                                        {
                                            int xx = int(x) + dx, yy = int(y) + dy;
                                            if (xx < 0 || yy < 0 || xx >= int(mw) || yy >= int(mh) ||
                                                original[yy * mw + xx] <= 0)
                                                boundary[y * mw + x] = true;
                                        }
                        auto expected = base;
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = (y * w + x) * 4;
                                float a = base[i + 3], best = 0, z = depth[i];
                                z = reversed ? z : 1 - z;
                                float sourceZ = 0;
                                // Brute-force every original cell; the GPU instead uses one shared-memory halo and
                                // local maxima.
                                for (unsigned sy = 0; sy < mh; ++sy)
                                    for (unsigned sx = 0; sx < mw; ++sx)
                                    {
                                        unsigned d = unsigned(
                                            std::max(std::abs(int(sx) - int(x / 4)), std::abs(int(sy) - int(y / 4))));
                                        if (d > n || seedDepth[sy * mw + sx] <= 0 || !boundary[sy * mw + sx])
                                            continue;
                                        float candidate = original[sy * mw + sx] * (1 - float(d) / float(n + 1));
                                        if (candidate > best)
                                        {
                                            best = candidate;
                                            sourceZ = seedDepth[sy * mw + sx];
                                        }
                                    }
                                if (std::isfinite(z) && z <= sourceZ * 1.05 + 1e-6)
                                    a = std::max(a, up(best));
                                a = std::max(base[i + 3], std::min(a, 1 - global));
                                expected[i + 3] = a;
                                for (unsigned c = 0; c < 3; ++c)
                                    expected[i + c] = filtered[c] * (1 - global - a) + raw[c] * (global + a);
                            }
                        f.r.begin();
                        auto b = f.texture(w, h, &base, DXGI_FORMAT_R16G16B16A16_FLOAT), p = f.texture(w, h, &proxy),
                             a = f.texture(w, h, &answer), d = f.texture(w, h, &depth, DXGI_FORMAT_R32_FLOAT),
                             field = f.texture(mw, mh, nullptr, DXGI_FORMAT_R32G32_FLOAT),
                             out = f.texture(w, h, nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
                        DlssNr::GhostRejection::Settings settings;
                        settings.method = 5;
                        settings.outwardMethod = 1;
                        settings.outwardLayers = n;
                        DlssNr::Superior::DlssNrConstants c {};
                        Fields::SetGhostOutwardFeather(c, w, h, settings, reversed, global, 0, false);
                        f.dispatch(hp.Get(), c, b.Get(), b.Get(), field.Get(), (mw + 7) / 8, (mh + 7) / 8, d.Get());
                        f.r.barrier(field.Get(), UAV, SRV);
                        Fields::SetGhostOutwardFeather(c, w, h, settings, reversed, global, 0, true);
                        if (inplace)
                        {
                            c.Mode = 59;
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
                            Near(got[i], expected[i], "coarse rings Chebyshev/opacity/depth/globalfade");
                            if (i % 4 == 3 && got[i] < base[i])
                                throw std::runtime_error("Coarse rings eroded mask");
                        }
                        ++checks;
                    }
    std::cout << "Coarse boundary rings PASS: " << checks
              << " cases, exact 1/2/3-layer opacity, post-contrast holes, occluders, odd borders, invalid depth and "
                 "both output paths\n";
}
