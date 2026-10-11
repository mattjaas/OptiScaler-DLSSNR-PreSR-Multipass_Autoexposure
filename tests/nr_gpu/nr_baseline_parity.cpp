#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/shaders/dlssnr/precompile/benchmark_v14/standard.h"

// Common baseline modes, not inter-pass: compare actual shipped v13/v14 and current standard DXIL.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: nr_baseline_parity current-shader-dir");
        Runner runner(true);
        auto current = runner.pipeline(argv[1], "DlssNr");
        ComPtr<ID3D12PipelineState> previous;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
        desc.pRootSignature = runner.root.Get();
        desc.CS = {nr_benchmark_v14_standard, sizeof(nr_benchmark_v14_standard)};
        check(runner.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&previous)));
        unsigned fixtures = 0;
        UINT64 components = 0;
        for (auto dims : {std::pair<UINT, UINT>{65,37}, {127,73}, {17,11}, {64,40}})
        for (unsigned mode : {0u, 1u, 2u, 7u, 8u, 9u, 10u, 18u, 20u, 21u, 22u, 23u, 24u, 25u, 26u})
        for (int stress = 0; stress < 4; ++stress)
        for (bool half : {false, true})
        {
            const UINT ww = std::max(1u, (dims.first * 65u + 50u) / 100u);
            const UINT wh = std::max(1u, (dims.second * 65u + 50u) / 100u);
            DlssNrConstants c {};
            c.Mode = mode;
            c.Width = mode == 0 || mode == 1 ? dims.first : ww;
            c.Height = mode == 0 || mode == 1 ? dims.second : wh;
            if (mode == 18) { c.Width = (ww + 7u) / 8u; c.Height = (wh + 7u) / 8u; }
            if (mode == 21 || mode == 24) { c.Width = 1; c.Height = 1; }
            c.WhitePoint = 1.0f; c.ReversibleMode = 2; c.Passthrough = stress % 2;
            c.ApplyModel = 1; c.Transfer = mode == 1 ? 9 : 0;
            c.TransferStrength = c.ColourStrength = 1.0f;
            c.MaxRatio = 4.0f; c.MaxDarkening = 100.0f;
            c.ResidualHistoryValid = 1; c.ResidualScale = 3.0f;
            c.ResidualBlend = 0.015f; c.ResidualConfidenceSensitivity = 1.0f;
            c.ExposureSourceWidth = dims.first; c.ExposureSourceHeight = dims.second;
            c.DirectDetailMode = ww; c.DirectResolveUpscaler = wh;
            c.ModelWorkScale = 0.65f;
            c.MvScaleX = 0.853497565f; c.MvScaleY = 0.829333186f;
            const auto a = runner.run(current.Get(), dims.first, dims.second, c.Width, c.Height,
                                      c, stress, half, 8, ww, wh);
            const auto b = runner.run(previous.Get(), dims.first, dims.second, c.Width, c.Height,
                                      c, stress, half, 8, ww, wh);
            for (size_t i = 0; i < a.size(); ++i)
            for (int shift : {0, half ? 16 : 0})
            {
                const auto x = half ? (a[i] >> shift) & 0xffffu : a[i];
                const auto y = half ? (b[i] >> shift) & 0xffffu : b[i];
                const bool bothNan = half ? (x & 0x7fffu) > 0x7c00u && (y & 0x7fffu) > 0x7c00u :
                                          (x & 0x7fffffffu) > 0x7f800000u && (y & 0x7fffffffu) > 0x7f800000u;
                if (x != y && !bothNan)
                {
                    std::cerr << "Baseline mismatch: mode=" << mode << " stress=" << stress
                              << " half=" << half << " component=" << i << '\n';
                    return 1;
                }
                if (!half) break;
            }
            components += a.size() * (half ? 2 : 1);
            ++fixtures;
        }
        std::cout << "Shipped v13/v14 versus current baseline parity PASS: " << fixtures
                  << " comparisons, " << components << " RGBA components\n";
        return 0;
    }
    catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
