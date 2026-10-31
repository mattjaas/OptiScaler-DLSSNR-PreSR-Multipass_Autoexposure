#pragma once
#include "DlssNr_PrivateExposure.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DlssNr::DepthTransport
{
struct Settings
{
    bool weightedGain = false, additive = false, fullRange = true;
    float nearLimit = 0.f, farLimit = 1.f, curve = .25f, bias = .1f;
    bool operator==(const Settings& b) const
    {
        return weightedGain == b.weightedGain && additive == b.additive && fullRange == b.fullRange &&
               nearLimit == b.nearLimit && farLimit == b.farLimit && curve == b.curve && bias == b.bias;
    }
    bool operator!=(const Settings& b) const { return !(*this == b); }
};
inline Settings Sanitize(Settings s)
{
    s.nearLimit = std::isfinite(s.nearLimit) ? std::clamp(s.nearLimit, 0.f, .9999f) : 0.f;
    s.farLimit = std::isfinite(s.farLimit) ? std::clamp(s.farLimit, s.nearLimit + .0001f, 1.f) : 1.f;
    s.curve = std::isfinite(s.curve) ? std::clamp(s.curve, .03125f, 64.f) : .25f;
    s.bias = std::isfinite(s.bias) ? std::clamp(s.bias, 0.f, 64.f) : .1f;
    return s;
}
inline uint32_t Flags(const Settings& s)
{
    const uint32_t active = (s.weightedGain ? 1u : 0u) | (s.additive && s.bias > 0 ? 2u : 0u);
    return active | (active && s.fullRange ? 4u : 0u);
}
inline Settings Effective(Settings s)
{
    s = Sanitize(s);
    if (!Flags(s))
        return {};
    if (!s.additive)
        s.bias = 0;
    if (s.fullRange)
    {
        s.nearLimit = 0;
        s.farLimit = 1;
    }
    return s;
}
template <class C> inline Settings Read(const C& c)
{
    return Sanitize(
        { c.DlssNrSuperiorDepthWeightedGain.value_or_default(), c.DlssNrSuperiorDepthBrightness.value_or_default(),
          c.DlssNrSuperiorDepthBrightnessFullRange.value_or_default(),
          c.DlssNrSuperiorDepthBrightnessNear.value_or_default(), c.DlssNrSuperiorDepthBrightnessFar.value_or_default(),
          c.DlssNrSuperiorDepthBrightnessCurve.value_or_default(),
          c.DlssNrSuperiorDepthBrightnessStrength.value_or_default() });
}
inline float Weight(float depth, bool inverted, Settings s)
{
    s = Sanitize(s);
    if (!Flags(s) || (!std::isfinite(depth) || depth < 0 || depth > 1))
        return 0;
    const float proximity = std::clamp(inverted ? depth : 1.f - depth, 0.f, 1.f);
    const float p = std::pow(proximity, s.curve);
    if (s.fullRange)
        return p;
    const float t = std::clamp((1.f - p - s.nearLimit) / (s.farLimit - s.nearLimit), 0.f, 1.f);
    return 1.f - t * t * (3.f - 2.f * t);
}
inline float Gain(float peak, float weight, const Settings& s)
{
    peak = PrivateExposure::PositiveGain(peak);
    return s.weightedGain ? 1.f + weight * (std::max(1.f, peak) - 1.f) : peak;
}
} // namespace DlssNr::DepthTransport
