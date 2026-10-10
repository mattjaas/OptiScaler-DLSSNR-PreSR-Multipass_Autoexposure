#pragma once
#include <array>

namespace DlssNr
{
// Short routine sweep selected from the 2026-10-10 v14 GPU measurements.
// Keep baseline (off/reference), intermediate controls and the useful winner.
struct InterPassBenchmarkCase
{
    int percent, mode;
    bool exact, tiled, compact, weights, mode28, rgb, linear20;
    const char* name;
};
inline constexpr std::array<InterPassBenchmarkCase, 14> kInterPassBenchmarkCases {{
    {50, 0, false, false, false, false, false, false, false, "No inter-pass"},
    {50, 2, false, false, false, false, false, false, false, "Fused reference"},
    {50, 2, true,  false, false, false, false, false, false, "Fused optimized"},
    {59, 0, false, false, false, false, false, false, false, "No inter-pass"},
    {59, 2, false, false, false, false, false, false, false, "Fused reference"},
    {59, 2, true,  true,  false, false, false, false, false, "Tiled linear 20"},
    {59, 2, true,  true,  false, true,  false, false, true,  "Linear20 weights + Mode28 v14"},
    {59, 2, true,  true,  false, true,  false, true,  true,  "Linear20 Mode28 + RGB v14"},
    {65, 0, false, false, false, false, false, false, false, "No inter-pass"},
    {65, 2, false, false, false, false, false, false, false, "Fused reference"},
    {65, 2, true,  true,  true,  false, false, false, false, "Tiled linear 16"},
    {65, 2, true,  true,  true,  true,  false, false, false, "Linear 16 + isolated weights v10"},
    {65, 2, true,  true,  true,  true,  true,  false, false, "Linear 16 + weights + dedicated Mode28 v13"},
    {65, 2, true,  true,  true,  true,  false, true,  false, "Linear16 Mode28 + RGB v14"},
}};
} // namespace DlssNr
