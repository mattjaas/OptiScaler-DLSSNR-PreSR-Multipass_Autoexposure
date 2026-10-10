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
// Targeted experiment, separate from the regular verification sweep.
inline constexpr std::array<int, 4> kInterPassLowBenchmarkScales {33, 40, 45, 49};
// Resolve partial RGB20 tile coverage; do not repeat already measured scales.
inline constexpr std::array<int, 2> kInterPassBoundaryBenchmarkScales {41, 42};
// A/B/C/C/B/A gives every candidate the same mean position within the sweep.
inline constexpr std::array<InterPass::Path, 6> kInterPassLowBenchmarkOrder {
    InterPass::Path::ClassicOptimized, InterPass::Path::FusedOptimized, InterPass::Path::Rgb20,
    InterPass::Path::Rgb20, InterPass::Path::FusedOptimized, InterPass::Path::ClassicOptimized
};
}
