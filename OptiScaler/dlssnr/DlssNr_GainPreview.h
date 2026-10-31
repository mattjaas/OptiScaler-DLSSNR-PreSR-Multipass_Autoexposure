#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace DlssNr::GainPreview
{
constexpr unsigned Bins = 1024, MotionSamples = 32 * 18, Groups = 8, TextureWidth = 257, TextureHeight = Groups * 2 + 2;
constexpr unsigned Coverage = 0, DepthBoundary = 1, BoundaryTextureWidth = 32, BoundaryTextureHeight = 10;
// Compact HLSL output ABI: two metadata entries, eight partial moments, 288 motion pairs.
static_assert(Groups == 8 && BoundaryTextureWidth == 32 && BoundaryTextureHeight == 10);
static_assert(2 + Groups + MotionSamples / 2 <= BoundaryTextureWidth * BoundaryTextureHeight);
constexpr float BoundaryTarget = .1429128154342723f;
constexpr float Epsilon = .008791791791791792f;
constexpr uint32_t BinBase = 0x33800000u >> 18; // 2^-24; 32 mantissa intervals per exponent
struct alignas(256) Constants
{
    uint32_t width = 0, height = 0, phaseX = 0, phaseY = 0;
    float motionThreshold = .1f, changeThreshold = .0625f;
    uint32_t requireChange = 1, previousValid = 0;
    uint32_t serialLow = 0, serialHigh = 0, gridX = 256, gridY = 144;
    uint32_t minMoving = 6, minChanged = 6, reserved0 = 0, reserved1 = 0;
    std::array<float, MotionSamples * 2> previous {};
};
static_assert(offsetof(Constants, previous) == 64 && sizeof(Constants) == 4864);
// Boundary probe only. Preserve the shared constant ABI / previous-motion block.
inline void SetBoundaryDepth(Constants& c, bool inverted, float precision)
{
    c.reserved0 = inverted ? 1u : 0u;
    precision = std::isfinite(precision) && precision >= 0 && precision <= .01f ? precision : 0.f;
    std::memcpy(&c.reserved1, &precision, 4);
}
static_assert(offsetof(Constants, reserved0) == 56 && offsetof(Constants, reserved1) == 60);
struct Settings
{
    float slowSeconds = 1.5f, fastSeconds = .25f, motionThreshold = .1f;
    bool requireChange = true;
    uint32_t method = Coverage;
    float boundaryTarget = BoundaryTarget;
};
inline float Seconds(float value, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, .05f, 10.f) : fallback;
}
inline Settings Sanitize(Settings s)
{
    s.slowSeconds = Seconds(s.slowSeconds, 1.5f);
    s.fastSeconds = std::min(Seconds(s.fastSeconds, .25f), s.slowSeconds);
    s.motionThreshold = std::isfinite(s.motionThreshold) ? std::clamp(s.motionThreshold, .001f, 10.f) : .1f;
    s.method = s.method == DepthBoundary ? DepthBoundary : Coverage;
    s.boundaryTarget = std::isfinite(s.boundaryTarget) ? std::clamp(s.boundaryTarget, .000001f, 16.f) : BoundaryTarget;
    return s;
}
template <class C> inline Settings Read(const C& c)
{
    return Sanitize({ c.DlssNrSuperiorGainPreviewSlowSeconds.value_or_default(),
                      c.DlssNrSuperiorGainPreviewFastSeconds.value_or_default(),
                      c.DlssNrSuperiorGainPreviewMotionThreshold.value_or_default(),
                      c.DlssNrSuperiorGainPreviewChangingMotion.value_or_default(),
                      c.DlssNrSuperiorGainPreviewMethod.value_or_default(),
                      c.DlssNrSuperiorGainPreviewBoundaryTarget.value_or_default() });
}
inline uint64_t Context(uint64_t base, Settings s)
{
    s = Sanitize(s);
    if (s.method == Coverage)
        return base;
    uint32_t bits;
    std::memcpy(&bits, &s.boundaryTarget, 4);
    return ((base ^ uint64_t(bits)) * 1099511628211ull) ^ 0x424f554e44415259ull;
}
inline double BoundaryGain(double offset, double slope, double target)
{
    if (!std::isfinite(offset) || !std::isfinite(slope) || !std::isfinite(target) || offset < 0 || slope < 0 ||
        target <= 0)
        return 0;
    if (offset + slope >= target)
        return 1;
    if (!(slope > 0))
        return 0;
    const double value = (target - offset) / slope;
    return std::isfinite(value) && value <= 1024 ? std::max(1., value) : 0;
}
inline double Target(bool transformer) { return transformer ? .9723433127521812 : .9205390681940847; }
struct Reading
{
    std::array<uint32_t, Bins> r {}, g {};
    std::array<float, MotionSamples * 2> motion {};
    uint32_t moving = 0, validMotion = 0, changed = 0, validEdits = 0, samples = 0, gate = 0;
    uint64_t serial = 0;
    double boundaryOffset = 0, boundarySlope = 0;
    uint32_t boundaryPairs = 0, boundaryCandidates = 0;
};
inline Reading Unpack(const void* data, size_t rowPitch)
{
    Reading s;
    const auto* bytes = static_cast<const char*>(data);
    for (unsigned group = 0; group < Groups; ++group)
    {
        uint32_t r[Bins], g[Bins], valid;
        std::memcpy(r, bytes + group * 2 * rowPitch, Bins * 4);
        std::memcpy(g, bytes + (group * 2 + 1) * rowPitch, Bins * 4);
        for (unsigned i = 0; i < Bins; ++i)
        {
            s.r[i] += r[i];
            s.g[i] += g[i];
        }
        std::memcpy(&valid, bytes + (group * 2 + 1) * rowPitch + 256 * 16, 4);
        s.validEdits += valid;
    }
    uint32_t a[4], b[4];
    std::memcpy(a, bytes + 256 * 16, 16);
    std::memcpy(b, bytes + rowPitch + 256 * 16, 16);
    s.moving = a[0];
    s.validMotion = a[1];
    s.samples = a[2];
    s.gate = b[1];
    s.changed = b[2];
    s.serial = uint64_t(a[3]) | (uint64_t(b[3]) << 32);
    // 288 uint4s contain two float2 samples each, stored after the partial histograms in two 257-wide rows.
    for (unsigned i = 0; i < 288; ++i)
        std::memcpy(s.motion.data() + i * 4, bytes + (Groups * 2 + i / 257) * rowPitch + (i % 257) * 16, 16);
    return s;
}
inline Reading UnpackBoundary(const void* data, size_t rowPitch)
{
    Reading s;
    const auto* bytes = static_cast<const char*>(data);
    auto at = [&](unsigned i)
    { return bytes + (i / BoundaryTextureWidth) * rowPitch + (i % BoundaryTextureWidth) * 16; };
    uint32_t a[4], b[4];
    std::memcpy(a, at(0), 16);
    std::memcpy(b, at(1), 16);
    s.moving = a[0];
    s.validMotion = a[1];
    s.samples = a[2];
    s.gate = b[1];
    s.changed = b[2];
    s.serial = uint64_t(a[3]) | (uint64_t(b[3]) << 32);
    if (s.gate == 1)
        for (unsigned group = 0; group < Groups; ++group)
        {
            float values[2];
            uint32_t counts[2];
            std::memcpy(values, at(2 + group), 8);
            std::memcpy(counts, at(2 + group) + 8, 8);
            s.boundaryOffset += values[0];
            s.boundarySlope += values[1];
            s.boundaryPairs += counts[0];
            s.boundaryCandidates += counts[1];
        }
    for (unsigned i = 0; i < 288; ++i)
        std::memcpy(s.motion.data() + i * 4, at(2 + Groups + i), 16);
    return s;
}
inline double Lower(unsigned bin)
{
    if (bin == 0)
        return 0;
    uint32_t bits = (BinBase + bin - 1) << 18;
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
inline unsigned Bin(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits >>= 18;
    return bits < BinBase ? 0 : 1 + std::min(1022u, bits - BinBase);
}
struct Estimate
{
    double value = 0, low = 0;
    bool valid = false;
};
inline Estimate Quantile(const std::array<double, Bins>& r, const std::array<double, Bins>& g, bool transformer)
{
    double totalR = 0, totalG = 0;
    for (unsigned i = 0; i < Bins; ++i)
    {
        totalR += r[i];
        totalG += g[i];
    }
    if (!(totalR > 0 && totalG > 0))
        return {};
    double cumulative = 0;
    for (unsigned i = 0; i < Bins; ++i)
    {
        cumulative += .5 * (r[i] / totalR + g[i] / totalG);
        if (cumulative <= 1 - Target(transformer))
            continue;
        if (i == 0 || i == Bins - 1)
            return {}; // no bounded inverse at under/overflow
        const double lo = Lower(i), hi = Lower(i + 1);
        const double gain = std::nextafter(float(Epsilon / lo), INFINITY);
        if (!(std::isfinite(gain) && gain >= .01 && gain <= 1024))
            return {};
        return { gain, Epsilon / hi, true }; // conservative edge, <=3.125% bin width
    }
    return {};
}
struct Status
{
    bool valid = false, paused = true, catchingUp = false;
    float smoothed = 0, raw = 0, motionPercent = 0, changedPercent = 0;
    double measuredAt = 0;
    uint64_t measurements = 0;
    uint32_t method = Coverage, boundaryPairs = 0;
    float boundaryBase = 0, boundarySlope = 0, boundaryTarget = BoundaryTarget;
    std::string text = "Preview disabled.";
};
class Filter
{
    std::array<double, Bins> histR {}, histG {};
    double lastTime = 0, logOutput = 0, boundaryBase = 0, boundarySlope = 0;
    unsigned changeStreak = 0;
    int boundaryDirection = 0;
    bool initialized = false;

  public:
    Status state;
    void Reset(const char* text = "Waiting for moving frames.")
    {
        *this = {};
        state.text = text;
    }
    void Pause(const char* text, double time)
    {
        state.paused = true;
        state.catchingUp = false;
        state.text = text;
        lastTime = time;
        changeStreak = 0;
        boundaryDirection = 0;
    }
    void UpdateBoundary(const Reading& s, Settings settings, double time)
    {
        state.boundaryPairs = s.boundaryPairs;
        if (s.boundaryPairs < 64 || s.boundaryPairs > s.boundaryCandidates || s.boundaryCandidates > 2ull * s.samples ||
            s.boundaryPairs < .95 * double(s.boundaryCandidates))
        {
            Pause("Waiting for 64 valid moving depth-boundary pairs.", time);
            return;
        }
        const double a = s.boundaryOffset / s.boundaryPairs, b = s.boundarySlope / s.boundaryPairs;
        const double raw = BoundaryGain(a, b, settings.boundaryTarget);
        if (!(raw > 0))
        {
            Pause("Depth-boundary target unreachable within 1..1024x.", time);
            return;
        }
        const double delta = initialized ? std::abs(std::log2(raw) - logOutput) : 0;
        const int direction = std::log2(raw) > logOutput ? 1 : -1;
        changeStreak = initialized && delta > .5 ? (direction == boundaryDirection ? changeStreak + 1 : 1) : 0;
        boundaryDirection = direction;
        const bool fast = changeStreak >= 2 || (state.catchingUp && delta > .07);
        const double dt = initialized ? std::clamp(time - lastTime, 0., .5) : 0;
        const double alpha = initialized ? -std::expm1(-dt / (fast ? settings.fastSeconds : settings.slowSeconds)) : 1;
        boundaryBase += alpha * (a - boundaryBase);
        boundarySlope += alpha * (b - boundarySlope);
        const double filtered = BoundaryGain(boundaryBase, boundarySlope, settings.boundaryTarget);
        if (!(filtered > 0))
        {
            Pause("Waiting for a bounded depth-boundary estimate.", time);
            return;
        }
        logOutput += alpha * (std::log2(filtered) - logOutput);
        initialized = true;
        lastTime = time;
        state.valid = true;
        state.paused = false;
        state.catchingUp = fast;
        state.raw = float(raw);
        state.smoothed = float(std::max(1., std::exp2(logOutput)));
        state.boundaryBase = float(boundaryBase);
        state.boundarySlope = float(boundarySlope);
        state.measuredAt = time;
        ++state.measurements;
        state.text = fast ? "Depth boundaries: catching up to sustained contrast change."
                          : "Measuring moving depth-boundary contrast (experimental).";
    }
    void Update(const Reading& s, bool transformer, Settings settings, double time)
    {
        settings = Sanitize(settings);
        state.method = settings.method;
        state.boundaryTarget = settings.boundaryTarget;
        state.motionPercent = 100.f * s.moving / MotionSamples;
        state.changedPercent = 100.f * s.changed / MotionSamples;
        if (s.gate != 1)
        {
            Pause(s.gate == 2   ? "Paused: motion field unchanged."
                  : s.gate == 3 ? "Waiting for motion-field baseline."
                                : "Paused: no usable motion.",
                  time);
            return;
        }
        if (settings.method == DepthBoundary)
        {
            UpdateBoundary(s, settings, time);
            return;
        }
        uint64_t nr = 0, ng = 0;
        for (unsigned i = 0; i < Bins; ++i)
        {
            nr += s.r[i];
            ng += s.g[i];
        }
        if (s.validEdits < s.samples * .95 || nr < 64 || ng < 64)
        {
            Pause("Waiting for enough valid positive R/G edit.", time);
            return;
        }
        std::array<double, Bins> r {}, g {};
        for (unsigned i = 0; i < Bins; ++i)
        {
            r[i] = double(s.r[i]) / nr;
            g[i] = double(s.g[i]) / ng;
        }
        const auto raw = Quantile(r, g, transformer);
        if (!raw.valid)
        {
            Pause("Estimate outside measurable 0.01..1024x range.", time);
            return;
        }
        double tv = 0;
        for (unsigned i = 0; i < Bins; ++i)
            tv += std::abs(r[i] - histR[i]) + std::abs(g[i] - histG[i]);
        tv *= .25;
        const double delta = initialized ? std::abs(std::log2(raw.value) - logOutput) : 0;
        changeStreak = initialized && tv > .15 && delta > .3 ? changeStreak + 1 : 0;
        const bool fast = changeStreak >= 2 || (state.catchingUp && delta > .07);
        const double dt = initialized ? std::clamp(time - lastTime, 0., .5) : 0;
        const double alpha = initialized ? -std::expm1(-dt / (fast ? settings.fastSeconds : settings.slowSeconds)) : 1;
        for (unsigned i = 0; i < Bins; ++i)
        {
            histR[i] += alpha * (r[i] - histR[i]);
            histG[i] += alpha * (g[i] - histG[i]);
        }
        const auto filtered = Quantile(histR, histG, transformer);
        if (!filtered.valid)
        {
            Pause("Waiting for a bounded R/G estimate.", time);
            return;
        }
        const double outAlpha =
            initialized ? -std::expm1(-dt / (fast ? settings.fastSeconds : settings.slowSeconds)) : 1;
        logOutput += outAlpha * (std::log2(filtered.value) - logOutput);
        initialized = true;
        lastTime = time;
        state.valid = true;
        state.paused = false;
        state.catchingUp = fast;
        state.smoothed = float(std::exp2(logOutput));
        state.raw = float(raw.value);
        state.measuredAt = time;
        ++state.measurements;
        state.text = fast ? "Measuring: catching up to a sustained change." : "Measuring moving frames.";
    }
};
} // namespace DlssNr::GainPreview
