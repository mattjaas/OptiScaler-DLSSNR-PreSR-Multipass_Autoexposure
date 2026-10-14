#pragma once
#include <algorithm>
#include <cmath>

namespace DlssNr::Profiles
{
// Calibrated high/mid gains describe Structure=1. Reduce their deviation from neutral
// proportionally below one; never extrapolate the calibration above Structure=1.
inline float StructureAdjustedHigh(float gain, float structure)
{
    gain = std::isfinite(gain) ? gain : 1.0f;
    const float factor = std::isfinite(structure) ? std::clamp(structure, 0.0f, 1.0f) : 1.0f;
    if (factor == 0.0f) return 1.0f;
    if (factor == 1.0f) return gain;
    return 1.0f + factor * (gain - 1.0f);
}
// Add reductions below the Structure=Intensity=1 calibration instead of multiplying them.
// Clamp each slider independently: values above 1 cannot cancel the other's reduction.
inline float ModelAdjustedHigh(float gain, float structure, float intensity)
{
    const float s = std::isfinite(structure) ? std::clamp(structure, 0.0f, 1.0f) : 1.0f;
    const float i = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 1.0f;
    // Algebraically max(0, 1 - ((1-s) + (1-i))); retain the exact old factor when i=1.
    return StructureAdjustedHigh(gain, std::max(0.0f, s - (1.0f - i)));
}
// Half-open model range [first, completed). A callback avoids a per-frame allocation.
template <class GainForPass>
inline float ProductGuidedHigh(unsigned first, unsigned completed, bool enabled, GainForPass gainForPass)
{
    if (completed <= first) return 1.0f;
    double product = 1.0;
    if (!enabled) product = gainForPass(completed - 1u);
    else
        for (unsigned pass = first; pass < completed; ++pass)
            product *= gainForPass(pass);
    const float result = static_cast<float>(product);
    return std::isfinite(result) ? result : 1.0f;
}
// Arithmetic mean of the effective per-model low gains; never a product.
template <class GainForPass>
inline float MeanGuidedLow(unsigned first, unsigned completed, GainForPass gainForPass)
{
    if (completed <= first) return 1.0f;
    double sum = 0.0;
    for (unsigned pass = first; pass < completed; ++pass)
        sum += gainForPass(pass);
    const float result = static_cast<float>(sum / (completed - first));
    return std::isfinite(result) ? result : 1.0f;
}
} // namespace DlssNr::Profiles
