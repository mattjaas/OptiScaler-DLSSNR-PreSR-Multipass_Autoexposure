#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace DlssNr::EditTransport
{
enum Mode : uint32_t
{
    PositiveOnly = 0,
    SymmetricToZero = 1,
    ShiftToZero = 2
};
inline uint32_t SanitizeMode(uint32_t mode) { return mode <= ShiftToZero ? mode : PositiveOnly; }
inline float AutoMaximum(float value) { return std::isfinite(value) ? std::clamp(value, 1.f, 1024.f) : 1024.f; }
inline uint32_t EffectiveMode(uint32_t mode, bool fullAnswer) { return fullAnswer ? PositiveOnly : SanitizeMode(mode); }
} // namespace DlssNr::EditTransport
