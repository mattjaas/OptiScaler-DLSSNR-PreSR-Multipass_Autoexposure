#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/shaders/dlssnr/precompile/benchmark_v14/standard.h"

// Check the low-frequency pass missing from the existing Mode27/28 parity matrix.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: interpass_revision_parity current-shader-dir");
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
        for (int stress = 0; stress < 4; ++stress)
        for (bool half : {false, true})
        {
            const UINT workW = std::max(1u, (dims.first * 65u + 50u) / 100u);
            const UINT workH = std::max(1u, (dims.second * 65u + 50u) / 100u);
            DlssNrConstants c {};
            c.Mode = 18; c.Width = (workW + 7u) / 8u; c.Height = (workH + 7u) / 8u;
            const auto a = runner.run(current.Get(), dims.first, dims.second, c.Width, c.Height,
                                      c, stress, half, 8, workW, workH);
            const auto b = runner.run(previous.Get(), dims.first, dims.second, c.Width, c.Height,
                                      c, stress, half, 8, workW, workH);
            for (size_t i = 0; i < a.size(); ++i)
            {
                for (int shift : {0, half ? 16 : 0})
                {
                    const auto x = half ? (a[i] >> shift) & 0xffffu : a[i];
                    const auto y = half ? (b[i] >> shift) & 0xffffu : b[i];
                    const bool bothNan = half ? (x & 0x7fffu) > 0x7c00u && (y & 0x7fffu) > 0x7c00u :
                                              (x & 0x7fffffffu) > 0x7f800000u && (y & 0x7fffffffu) > 0x7f800000u;
                    if (x != y && !bothNan) throw std::runtime_error("Archived Mode18 parity mismatch");
                    if (!half) break;
                }
            }
            components += a.size() * (half ? 2 : 1);
            ++fixtures;
        }
        std::cout << "Archived Mode18 parity PASS: " << fixtures << " comparisons, "
                  << components << " RGBA components\n";
        return 0;
    }
    catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
