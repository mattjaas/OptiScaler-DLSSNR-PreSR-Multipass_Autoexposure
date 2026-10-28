#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Explicit experimental choice, independent of the model selected by NGX.
namespace DlssNr::PrivateExposure
{
enum Mode : uint32_t
{
    Native = 0,
    Unit,
    Manual,
    LegacyCNN,
    LegacyK,
    Transformer2,
    Count
};
inline uint32_t SanitizeMode(uint32_t mode) { return mode < Count ? mode : Native; }
inline float PositiveGain(float value) { return std::isfinite(value) ? std::clamp(value, .01f, 1024.f) : 1.f; }
inline float EditSaturation(float value) { return std::isfinite(value) ? std::clamp(value, 1.f, 64.f) : 1.f; }
inline float DepthStrength(float value) { return std::isfinite(value) ? std::clamp(value, 0.f, 16.f) : 1.f; }
inline float DepthCurve(float value) { return std::isfinite(value) ? std::clamp(value, 0.03125f, 64.f) : 1.f; }
inline uint32_t DepthPair(uint32_t pair) { return pair < 3 ? pair : 0; }
inline float ManualValue(float value)
{
    return std::isfinite(value) && value > 0.f ? std::clamp(value, 0.000001f, 65504.f) : 1.f;
}
inline uint32_t SampleExtent(uint32_t render, uint32_t output, uint32_t mode)
{
    const uint32_t half = output / 2;
    return std::max(2u, mode == LegacyK || half <= render ? half : std::max(render / 2, 256u));
}
inline bool Measure(uint32_t mode, uint64_t evaluation, bool reset)
{
    return reset || mode == Transformer2 || (evaluation & 1u) == 0;
}
} // namespace DlssNr::PrivateExposure
