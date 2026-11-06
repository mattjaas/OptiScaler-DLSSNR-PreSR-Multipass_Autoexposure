// Analytic pinhole projection of a horizontal road while the camera advances and pitches upward.
// The expected visibility timer follows the projected point, independent of NDC depth change.
void PitchRoadQuality(Fixture& f, bool reportOnly = false, bool baseline = false)
{
    unsigned errors = 0, active = 0, sequences = 0;
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
            for (unsigned bits : { 0u, 16u, 24u })
                for (double pitch : { .01, .025, -.015 })
                {
                    constexpr unsigned w = 320, h = 240;
                    const unsigned step = variant == 3 ? 4 : 2;
                    TemporalGhostFixture t(f, w, h, variant != 0, variant == 2, step);
                    if (baseline)
                        t.age =
                            f.r.pipeline("artifacts/ghost-study/pitch-baseline", variant == 3 ? "private_ghost_coarse_2"
                                                                                 : variant ? "private_ghost_compact_2"
                                                                                           : "private_ghost_history");
                    std::vector<unsigned> expected(t.mw * t.mh, 0);
                    std::vector<float> lastZ, lastMv;
                    std::vector<bool> oldForeground(expected.size(), false);
                    for (unsigned frame = 0; frame < 10; ++frame)
                    {
                        const double angle = (pitch > 0 ? .30 : .12) - double(frame) * pitch, oldAngle = angle + pitch,
                                     focal = 180, nearHeight = .015, dzNear = 8;
                        const double delta = angle - oldAngle, sd = std::sin(delta), cd = std::cos(delta);
                        std::vector<float> z(w * h * 4, 0), mv(z);
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x)
                            {
                                size_t i = (y * w + x) * 4;
                                bool object = x >= 120 && x < 196 && y >= 64 && y < 156;
                                double sy = (double(y) + .5 - h * .5) / focal, sx = (double(x) + .5 - w * .5) / focal;
                                double depth = nearHeight * (std::sin(angle) + sy * std::cos(angle));
                                depth = std::max(depth, 1e-5);
                                double denominator = cd - sy * sd + dzNear * depth * std::cos(oldAngle);
                                double px = sx / denominator,
                                       py = (sd + sy * cd - dzNear * depth * std::sin(oldAngle)) / denominator;
                                float n = object ? .06f : float(depth);
                                float zv = reversed ? n : 1 - n;
                                if (bits)
                                    zv = std::round(zv * ((1u << bits) - 1)) / float((1u << bits) - 1);
                                z[i] = zv;
                                if (!object)
                                {
                                    mv[i] = float((px - sx) * focal);
                                    mv[i + 1] = float((py - sy) * focal);
                                }
                            }
                        auto result = t.FrameGuides(z, mv, false, reversed, bits, 18, 12, .05f, 2.5f);
                        auto old = expected;
                        auto oldFg = oldForeground;
                        for (unsigned y = 0; y < t.mh; ++y)
                            for (unsigned x = 0; x < t.mw; ++x)
                            {
                                size_t cell = y * t.mw + x;
                                unsigned chosen = 0;
                                float best = -1;
                                for (unsigned dy = 0; dy < step; ++dy)
                                    for (unsigned dx = 0; dx < step; ++dx)
                                    {
                                        unsigned xx = x * step + dx, yy = y * step + dy;
                                        if (xx >= w || yy >= h)
                                            continue;
                                        size_t i = (yy * w + xx) * 4;
                                        float n = reversed ? z[i] : 1 - z[i];
                                        if (n > best)
                                        {
                                            best = n;
                                            chosen = unsigned(i / 4);
                                        }
                                    }
                                unsigned sx = chosen % w, sy = chosen / w;
                                bool fg = sx >= 120 && sx < 196 && sy >= 64 && sy < 156;
                                oldForeground[cell] = fg;
                                float mx = mv[chosen * 4], my = mv[chosen * 4 + 1];
                                if (variant)
                                {
                                    mx = mx == 0 ? 0 : std::copysign(ReferenceHalf(std::abs(mx)), mx);
                                    my = my == 0 ? 0 : std::copysign(ReferenceHalf(std::abs(my)), my);
                                }
                                int ox = int(std::nearbyint((sx + .5f + mx) / step - .5f)),
                                    oy = int(std::nearbyint((sy + .5f + my) / step - .5f));
                                expected[cell] = 0;
                                if (frame && !fg && ox >= 0 && oy >= 0 && ox < int(t.mw) && oy < int(t.mh))
                                {
                                    size_t predecessor = oy * t.mw + ox;
                                    expected[cell] = oldFg[predecessor]     ? 18
                                                     : old[predecessor] > 0 ? old[predecessor] - 1
                                                                            : 0;
                                }
                                bool boundary =
                                    std::abs(int(sx) - 120) <= int(step) || std::abs(int(sx) - 196) <= int(step) ||
                                    std::abs(int(sy) - 64) <= int(step) || std::abs(int(sy) - 156) <= int(step);
                                if (sy < 70 || boundary || best < .0001f)
                                    continue;
                                if (expected[cell])
                                    ++active;
                                if (result[cell * 4 + 1] != expected[cell])
                                {
                                    ++errors;
                                    if (!reportOnly)
                                        throw std::runtime_error(
                                            "Pitch-road timer cut variant=" + std::to_string(variant) +
                                            " reversed=" + std::to_string(reversed) + " bits=" + std::to_string(bits) +
                                            " pitch=" + std::to_string(pitch) + " frame=" + std::to_string(frame) +
                                            " x=" + std::to_string(sx) + " y=" + std::to_string(sy) +
                                            " got=" + std::to_string(result[cell * 4 + 1]) +
                                            " expected=" + std::to_string(expected[cell]));
                                }
                            }
                        lastZ = z;
                        lastMv = mv;
                    }
                    if (!baseline)
                    {
                        auto closer = lastZ;
                        for (unsigned y = 180; y < 228; ++y)
                            for (unsigned x = 116; x < 216; ++x)
                            {
                                float depth = reversed ? .09f : .91f;
                                if (bits)
                                    depth = std::round(depth * ((1u << bits) - 1)) / float((1u << bits) - 1);
                                closer[(y * w + x) * 4] = depth;
                            }
                        auto foreground = t.FrameGuides(closer, lastMv, false, reversed, bits, 18, 12, .05f, 2.5f);
                        for (unsigned y = 188 / step; y < 220 / step; ++y)
                            for (unsigned x = 124 / step; x < 208 / step; ++x)
                                if (foreground[(y * t.mw + x) * 4 + 1] != 0)
                                    throw std::runtime_error(
                                        "Pitch continuation spread a road timer onto new closer geometry, variant=" +
                                        std::to_string(variant));
                        std::vector<float> stopped(lastMv.size(), 0);
                        for (unsigned tick = 0; tick <= 18; ++tick)
                        {
                            auto expired = t.FrameGuides(lastZ, stopped, false, reversed, bits, 18, 12, .05f, 2.5f);
                            if (tick == 18)
                                for (size_t i = 1; i < expired.size(); i += 4)
                                    if (expired[i] != 0)
                                        throw std::runtime_error("Pitch continuation renewed an expired/stopped mask");
                        }
                    }
                    ++sequences;
                }
    if (baseline && !errors)
        throw std::runtime_error("Pinned reference did not reproduce pitch-road mask loss");
    std::cout << "Pitch-road " << (reportOnly ? "audit" : "PASS") << ": " << sequences << " sequences, " << active
              << " active, " << errors << " incorrect timers\n";
}
