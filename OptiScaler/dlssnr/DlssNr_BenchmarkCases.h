#pragma once
#include <array>

namespace DlssNr
{
// Short routine sweep selected from the 2026-10-10 v14 GPU measurements.
// Keep baselines and the measured v14 winner; replace old controls with v16 experiments.
struct InterPassBenchmarkCase
{
    int percent, mode;
    bool exact, tiled, compact, weights, mode28, rgb, linear20, wide, pair, low;
    const char* name;
};
inline constexpr std::array<InterPassBenchmarkCase, 17> kInterPassBenchmarkCases {{
    {50, 0, false, false, false, false, false, false, false, false, false, false, "No inter-pass"},
    {50, 2, false, false, false, false, false, false, false, false, false, false, "Fused reference"},
    {50, 2, true,  false, false, false, false, false, false, false, false, false, "Fused optimized"},
    {59, 0, false, false, false, false, false, false, false, false, false, false, "No inter-pass"},
    {59, 2, false, false, false, false, false, false, false, false, false, false, "Fused reference"},
    {59, 2, true, true, false, true, false, true, true, false, false, false, "Linear20 Mode28 + RGB v14"},
    {59, 2, true, true, false, true, false, true, true, true, false, false, "RGB + wide tile v16"},
    {59, 2, true, true, false, true, false, true, true, false, true, false, "RGB + pair loads v16"},
    {59, 2, true, true, false, true, false, true, true, true, true, false, "RGB + wide + pair v16"},
    {59, 2, true, true, false, true, false, true, true, false, false, true, "RGB + dedicated Mode18 v16"},
    {65, 0, false, false, false, false, false, false, false, false, false, false, "No inter-pass"},
    {65, 2, false, false, false, false, false, false, false, false, false, false, "Fused reference"},
    {65, 2, true, true, true, true, false, true, false, false, false, false, "Linear16 Mode28 + RGB v14"},
    {65, 2, true, true, true, true, false, true, false, true, false, false, "RGB + wide tile v16"},
    {65, 2, true, true, true, true, false, true, false, false, true, false, "RGB + pair loads v16"},
    {65, 2, true, true, true, true, false, true, false, true, true, false, "RGB + wide + pair v16"},
    {65, 2, true, true, true, true, false, true, false, false, false, true, "RGB + dedicated Mode18 v16"},
}};
} // namespace DlssNr
