#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/shaders/dlssnr/precompile/benchmark_v14/guide16.h"

// Compare the exact archived screenshot route against the current RGB16 output.
// Synthetic parity is not a measurement of the temporal NGX chain or GPU speed.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: interpass_guide16_parity current-shader-dir");
        Runner runner(true);
        auto current = runner.pipeline(argv[1], "dlssnr_interpass_rgb16");
        ComPtr<ID3D12PipelineState> guide;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
        desc.pRootSignature = runner.root.Get();
        desc.CS = {nr_benchmark_v14_guide16, sizeof(nr_benchmark_v14_guide16)};
        check(runner.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&guide)));
        unsigned fixtures = 0;
        UINT64 components = 0;
        for (auto dims : {std::pair<UINT, UINT>{65,37}, {127,73}, {17,11}, {64,40}, {3840,2160}})
        for (int stress = 0; stress < 4; ++stress)
        for (int gains = 0; gains < 3; ++gains)
        for (bool half : {false, true})
        {
            // One stress pattern at full native geometry; all stress patterns at odd edges.
            if (dims.first == 3840 && stress != 0) continue;
            const UINT ww = std::max(1u, (dims.first * 65u + 50u) / 100u);
            const UINT wh = std::max(1u, (dims.second * 65u + 50u) / 100u);
            DlssNrConstants c {};
            c.Mode = 28; c.Width = ww; c.Height = wh;
            c.ResidualHistoryValid = 1; c.ResidualScale = 3.0f;
            c.ResidualBlend = stress == 2 ? 0.00001f : stress == 1 ? 0.2f : 0.015f;
            c.ResidualConfidenceSensitivity = 1.0f;
            c.DirectDetailMode = ww; c.DirectResolveUpscaler = wh;
            const float rx = float(ww) / dims.first, ry = float(wh) / dims.second;
            std::memcpy(&c.ResidualMotionBaseX, &rx, 4);
            std::memcpy(&c.ResidualMotionBaseY, &ry, 4);
            c.DirectResolveFlags = 16u | 64u | 128u | 2048u | 8192u;
            if (gains != 0)
            {
                c.GuideWidth = 1;
                c.MvScaleX = gains == 1 ? 0.85f : 0.853497565f;
                c.MvScaleY = gains == 1 ? 0.85f : 0.829333186f;
            }
            if (stress == 2)
            {
                c.GuideHeight = 1;
                const float low = 0.02f, high = 0.08f, floor = 0.15f;
                std::memcpy(&c.ExposureSourceWidth, &low, 4);
                std::memcpy(&c.ExposureSourceHeight, &high, 4);
                std::memcpy(&c.ExposurePadding, &floor, 4);
            }
            auto oldConstants = c;
            oldConstants.DirectResolveFlags |= 16384u | 16777216u;
            oldConstants.DirectResolveFlags &= ~4194304u;
            const auto a = runner.run(current.Get(), dims.first, dims.second, ww, wh, c, stress, half, 8, ww, wh);
            const auto b = runner.run(guide.Get(), dims.first, dims.second, ww, wh, oldConstants, stress, half, 8, ww, wh);
            for (size_t i = 0; i < a.size(); ++i)
            {
                for (int shift : {0, half ? 16 : 0})
                {
                    const auto x = half ? (a[i] >> shift) & 0xffffu : a[i];
                    const auto y = half ? (b[i] >> shift) & 0xffffu : b[i];
                    const bool bothNan = half ? (x & 0x7fffu) > 0x7c00u && (y & 0x7fffu) > 0x7c00u :
                                              (x & 0x7fffffffu) > 0x7f800000u && (y & 0x7fffffffu) > 0x7f800000u;
                    if (x != y && !bothNan)
                    {
                        std::cerr << "Guide16 mismatch: native=" << dims.first << 'x' << dims.second
                                  << " stress=" << stress << " gains=" << gains << " half=" << half
                                  << " component=" << i << '\n';
                        return 1;
                    }
                    if (!half) break;
                }
            }
            components += a.size() * (half ? 2 : 1);
            ++fixtures;
        }
        std::cout << "Archived Guide16 versus current RGB16 parity PASS: " << fixtures
                  << " comparisons, " << components << " RGBA components\n";
        return 0;
    }
    catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
