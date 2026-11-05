// Constant-speed forward camera translation over a planar road, with a co-moving foreground.
// Independently project the plane with a pinhole camera; do not infer expected timers from a shader.
void RoadVisibilityQuality(Fixture& f, bool baseline = false)
{
    unsigned sequences = 0, errors = 0, active = 0;
    for (unsigned variant : { 0u, 1u, 2u, 3u })
        for (bool reversed : { false, true })
            for (unsigned bits : { 0u, 16u, 24u })
            {
                constexpr unsigned w = 256, h = 192;
                const unsigned step = variant == 3 ? 4 : 2;
                TemporalGhostFixture t(f, w, h, variant != 0, variant == 2, step);
                if (baseline)
                    t.age = f.r.pipeline("artifacts/ghost-study/road-baseline", variant == 3 ? "private_ghost_coarse_2"
                                                                                : variant    ? "private_ghost_compact_2"
                                                                                             : "private_ghost_history");
                std::vector<float> z(w * h * 4, 0), mv(z);
                // z_near = near/Z = .00025*(y+.5-horizon). dz_camera/near = 6.
                // Projection into the previous camera scales x/y about the principal point by 1/(1+6*z_near).
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x)
                    {
                        size_t i = (size_t(y) * w + x) * 4;
                        bool object = x >= 96 && x < 160 && y >= 48 && y < 152;
                        float road = .00025f * (float(y) + .5f - 16.f);
                        float nearZ = object ? .06f : std::max(.0001f, road);
                        float depth = reversed ? nearZ : 1 - nearZ;
                        if (bits)
                            depth = std::round(depth * ((1u << bits) - 1)) / float((1u << bits) - 1);
                        z[i] = depth;
                        if (!object && road > 0)
                        {
                            float k = 1.f / (1 + 6 * road);
                            mv[i] = (float(x) + .5f - 128) * (k - 1);
                            mv[i + 1] = (float(y) + .5f - 16) * (k - 1);
                        }
                    }
                // Determine the known foreground support of each depth footprint and its selected motion.
                std::vector<int> predecessor(t.mw * t.mh, -1);
                std::vector<bool> foreground(predecessor.size(), false);
                for (unsigned y = 0; y < t.mh; ++y)
                    for (unsigned x = 0; x < t.mw; ++x)
                    {
                        float best = -1;
                        unsigned chosen = 0;
                        for (unsigned dy = 0; dy < step; ++dy)
                            for (unsigned dx = 0; dx < step; ++dx)
                            {
                                unsigned sx = std::min(x * step + dx, w - 1), sy = std::min(y * step + dy, h - 1);
                                size_t i = (sy * w + sx) * 4;
                                float nearZ = reversed ? z[i] : 1 - z[i];
                                if (nearZ > best)
                                {
                                    best = nearZ;
                                    chosen = unsigned(i / 4);
                                }
                            }
                        unsigned sx = chosen % w, sy = chosen / w;
                        float mx = mv[chosen * 4], my = mv[chosen * 4 + 1];
                        if (variant)
                        {
                            mx = mx == 0 ? 0 : std::copysign(ReferenceHalf(std::abs(mx)), mx);
                            my = my == 0 ? 0 : std::copysign(ReferenceHalf(std::abs(my)), my);
                        }
                        const int ox = int(std::nearbyint((sx + .5f + mx) / step - .5f)),
                                  oy = int(std::nearbyint((sy + .5f + my) / step - .5f));
                        size_t i = y * t.mw + x;
                        foreground[i] = sx >= 96 && sx < 160 && sy >= 48 && sy < 152;
                        if (ox >= 0 && oy >= 0 && ox < int(t.mw) && oy < int(t.mh))
                            predecessor[i] = oy * t.mw + ox;
                    }
                std::vector<unsigned> expected(predecessor.size(), 0);
                std::vector<float> last(predecessor.size() * 4, 0);
                for (unsigned frame = 0; frame < 12; ++frame)
                {
                    auto result = t.FrameGuides(z, mv, false, reversed, bits, 18, 12, .05f, 2.f);
                    auto old = expected;
                    for (size_t i = 0; i < expected.size(); ++i)
                    {
                        expected[i] = 0;
                        int previous = predecessor[i];
                        if (frame && !foreground[i] && previous >= 0)
                            expected[i] = foreground[previous] ? 18 : old[previous] > 0 ? old[previous] - 1 : 0;
                        // The interior of the valid road excludes sky and the sampling uncertainty immediately at
                        // silhouettes.
                        unsigned px = unsigned(i % t.mw) * step, py = unsigned(i / t.mw) * step;
                        bool boundary = std::abs(int(px) - 96) <= int(step) || std::abs(int(px) - 160) <= int(step) ||
                                        std::abs(int(py) - 48) <= int(step) || std::abs(int(py) - 152) <= int(step);
                        if (py < 48 || boundary)
                            continue;
                        if (expected[i] > 0)
                            ++active;
                        if (result[i * 4 + 1] != expected[i])
                        {
                            ++errors;
                            if (!baseline)
                                throw std::runtime_error(
                                    "Forward-road timer failed variant=" + std::to_string(variant) +
                                    " Z=" + std::to_string(reversed) + " bits=" + std::to_string(bits) +
                                    " frame=" + std::to_string(frame) + " x=" + std::to_string(px) +
                                    " y=" + std::to_string(py) + " age=" + std::to_string(result[i * 4 + 1]) +
                                    " expected=" + std::to_string(expected[i]) +
                                    " previous=" + std::to_string(previous) +
                                    " oldactual=" + std::to_string(previous >= 0 ? last[previous * 4 + 1] : -1.f) +
                                    " olddepth=" + std::to_string(previous >= 0 ? last[previous * 4] : -1.f) +
                                    " depth=" + std::to_string(result[i * 4]) + " mv=" +
                                    std::to_string(result[i * 4 + 2]) + "," + std::to_string(result[i * 4 + 3]));
                        }
                    }
                    last = result;
                }
                if (!baseline)
                {
                    // New closer geometry must still clear the road timer, even when given the road's MV.
                    auto closer = z;
                    for (unsigned y = 160; y < 190; ++y)
                        for (unsigned x = 164; x < 224; ++x)
                        {
                            float value = reversed ? .09f : .91f;
                            if (bits)
                                value = std::round(value * ((1u << bits) - 1)) / float((1u << bits) - 1);
                            closer[(y * w + x) * 4] = value;
                        }
                    auto result = t.FrameGuides(closer, mv, false, reversed, bits, 18, 12, .05f, 2.f);
                    for (unsigned y = 164 / step; y < 184 / step; ++y)
                        for (unsigned x = 168 / step; x < 220 / step; ++x)
                            if (result[(y * t.mw + x) * 4 + 1] != 0)
                                throw std::runtime_error("New closer geometry inherited an advancing-road timer");
                }
                if (!baseline)
                {
                    std::vector<float> stopped(mv.size(), 0);
                    for (unsigned frame = 0; frame <= 18; ++frame)
                    {
                        auto result = t.FrameGuides(z, stopped, false, reversed, bits, 18, 12, .05f, 2.f);
                        if (frame == 18)
                            for (size_t i = 1; i < result.size(); i += 4)
                                if (result[i] != 0)
                                    throw std::runtime_error("Road continuation renewed a stopped timer beyond N");
                    }
                }
                ++sequences;
            }
    if (baseline && !errors)
        throw std::runtime_error("Pinned baseline failed to reproduce forward-road mask loss");
    std::cout << "Forward-camera road " << (baseline ? "baseline" : "PASS") << ": " << sequences << " sequences, "
              << active << " active reference samples, " << errors << " incorrect timers\n";
}
