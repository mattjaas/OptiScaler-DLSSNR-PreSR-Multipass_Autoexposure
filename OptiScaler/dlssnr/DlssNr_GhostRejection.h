#pragma once
#include <algorithm>
#include <cmath>

namespace DlssNr::GhostRejection
{
struct Settings
{
    bool enabled = false;
    float threshold = .01f, softness = .02f, strength = 1.f;
};
inline Settings Sanitize(Settings s)
{
    s.threshold = std::isfinite(s.threshold) ? std::clamp(s.threshold, 0.f, .25f) : .01f;
    s.softness = std::isfinite(s.softness) ? std::clamp(s.softness, .0001f, .25f) : .02f;
    s.strength = std::isfinite(s.strength) ? std::clamp(s.strength, 0.f, 1.f) : 1.f;
    return s;
}
template <class Config> Settings Read(const Config& c)
{
    return Sanitize(
        { c.DlssNrSuperiorGhostRejection.value_or_default(), c.DlssNrSuperiorGhostThreshold.value_or_default(),
          c.DlssNrSuperiorGhostSoftness.value_or_default(), c.DlssNrSuperiorGhostStrength.value_or_default() });
}
} // namespace DlssNr::GhostRejection
