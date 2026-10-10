#pragma once
#include "DlssNr_InterPassPolicy.h"
#include <array>
namespace DlssNr
{
struct InterPassBenchmarkScale { int percent; InterPass::Path expected; double historicalGainVsFusedMs; };
// Last supplied RTX4090/4K/six-pass/guide-one measurements, not a guarantee.
inline constexpr std::array<InterPassBenchmarkScale, 3> kInterPassBenchmarkScales {{
    {50, InterPass::Path::FusedOptimized, 1.74439},
    {59, InterPass::Path::Rgb20, 3.53894},
    {65, InterPass::Path::Rgb16, 4.35814},
}};
}
