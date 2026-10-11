#pragma once
#include <algorithm>
#include <cmath>

namespace DlssNr::Profiles
{
// Shared by ordinary final resolve and grouped inter-pass high/mid shaping.
inline float CompoundGuidedHigh(float singlePassGain, unsigned passes, bool enabled)
{
    const float gain = enabled ? std::pow(singlePassGain, static_cast<float>(std::max(1u, passes))) : singlePassGain;
    return std::isfinite(gain) ? gain : 1.0f;
}
} // namespace DlssNr::Profiles
