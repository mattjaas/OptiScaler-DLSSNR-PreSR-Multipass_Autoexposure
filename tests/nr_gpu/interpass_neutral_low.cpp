#include "pch.h"
#include "interpass_fixture.h"
#include <cmath>

// Constant signed residual has only a low band. Neutral low must preserve it,
// regardless of high gain, on all existing inter-pass shader implementations.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: interpass_neutral_low shader-dir");
        Runner r(true);
        auto standard = r.pipeline(argv[1], "DlssNr");
        auto low = r.pipeline(argv[1], "dlssnr_interpass_low36");
        auto cached = r.pipeline(argv[1], "dlssnr_interpass_low36_cached");
        unsigned checks = 0;
        for (auto dims : {std::pair<UINT,UINT>{65,37}, {127,73}})
        for (bool half : {false, true})
        for (int sign : {-1, 1})
        for (auto* lowPso : {low.Get(), cached.Get()})
        {
            const UINT ww = (dims.first + 1) / 2, wh = (dims.second + 1) / 2;
            const UINT lw = (ww + 7) / 8, lh = (wh + 7) / 8;
            const int fixture = sign < 0 ? 1 : 0;
            DlssNrConstants c {};
            c.Mode = 34; c.Width = lw; c.Height = lh;
            const auto field = r.run(lowPso, dims.first, dims.second, lw, lh, c, fixture, half, 8, ww, wh, nullptr, 1);
            for (UINT mode : {27u, 28u, 33u, 36u})
            for (float high : {0.0f, 0.454f, 1.0f, 1.5f})
            for (float lowGain : {1.0f, 0.71f})
            {
                c = {}; c.Mode = mode;
                c.Width = mode == 27 ? dims.first : ww;
                c.Height = mode == 27 ? dims.second : wh;
                c.ResidualHistoryValid = 1; c.ResidualBlend = 0.015f;
                c.ResidualScale = 3.0f; c.ResidualConfidenceSensitivity = 1.0f;
                c.GuideWidth = 1; c.MvScaleX = high; c.MvScaleY = lowGain;
                const bool uniform = high == lowGain;
                if (uniform) { c.GuideWidth = 0; c.DirectResolveFlags = 32; }
                const auto output = r.run(standard.Get(), dims.first, dims.second, c.Width, c.Height, c,
                    fixture, half, 8, ww, wh, nullptr, 1, uniform ? nullptr : &field, lw, lh);
                const float expected = 0.4f + sign * 0.08f * lowGain;
                for (size_t i = 0; i < size_t(c.Width) * c.Height * 4; ++i)
                {
                    float value;
                    if (!half) std::memcpy(&value, &output[i], 4);
                    else
                    {
                        const UINT bits = (output[i/2] >> (16*(i%2))) & 0xffff;
                        const UINT e = (bits >> 10) & 31, m = bits & 1023;
                        if (e == 31) throw std::runtime_error("Nonfinite FP16 result");
                        value = e ? std::ldexp(float(1024+m), int(e)-25) : std::ldexp(float(m), -24);
                        if (bits & 32768) value = -value;
                    }
                    const float target = i % 4 == 3 ? 0.7f : expected;
                    if (!std::isfinite(value) || std::abs(value-target) > (half ? 0.0005f : 0.000002f))
                        throw std::runtime_error("Low-band gain or alpha changed unexpectedly");
                }
                ++checks;
            }
        }
        std::cout << "Neutral inter-pass low GPU PASS: " << checks
                  << " constant-band checks; gain 1 versus configured gain, signed residual, high independence and alpha\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
