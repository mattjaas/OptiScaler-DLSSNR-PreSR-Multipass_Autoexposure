#pragma once
#include "DlssNr_GainPreview.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace DlssNr::LowMotionReset
{
struct Settings
{
    bool enabled = false;
    float threshold = .25f, seconds = 2.f; // pixels at 1080p reference height / frame; seconds of quiet
    uint32_t minimumMovingPercent = 75;
};
inline Settings Sanitize(Settings s)
{
    s.threshold = std::isfinite(s.threshold) ? std::clamp(s.threshold, .001f, 64.f) : .25f;
    s.seconds = std::isfinite(s.seconds) ? std::clamp(s.seconds, .05f, 60.f) : 2.f;
    s.minimumMovingPercent = std::clamp(s.minimumMovingPercent, 1u, 100u);
    return s;
}
template <class C> inline Settings Read(const C& c)
{
    return Sanitize({ c.DlssNrSuperiorLowMotionReset.value_or_default(),
                      c.DlssNrSuperiorLowMotionResetThreshold.value_or_default(),
                      c.DlssNrSuperiorLowMotionResetSeconds.value_or_default(),
                      c.DlssNrSuperiorLowMotionMinimumPercent.value_or_default() });
}
inline bool BelowMotionThreshold(const Settings& settings, const GainPreview::Status& sample)
{
    if (sample.method == GainPreview::MotionWatch)
        return std::isfinite(sample.motionPercent) && sample.motionPercent >= 0.f &&
               sample.motionPercent < float(settings.minimumMovingPercent);
    return sample.raw <= settings.threshold; // legacy vector-magnitude samples used by standalone controller callers
}

struct Status
{
    bool enabled = false, triggered = false, sharedProbe = false;
    float peak = 0, quietSeconds = 0, movingPercent = 0;
    uint64_t requests = 0;
    std::string text = "Low-motion reset disabled.";
};
class Controller
{
    Settings previous;
    bool configured = false;
    uint64_t context = 0, lastSample = 0;
    double quietStart = -1, lastTime = -1;
    void BreakQuiet()
    {
        quietStart = -1;
        lastTime = -1;
        state.quietSeconds = 0;
    }

  public:
    Status state;
    bool Update(Settings settings, const GainPreview::Status& sample, double now, uint64_t key, bool interrupted,
                bool shared)
    {
        settings = Sanitize(settings);
        state.triggered = false;
        state.sharedProbe = shared;
        if (!configured || context != key || previous.enabled != settings.enabled ||
            previous.threshold != settings.threshold || previous.seconds != settings.seconds ||
            previous.minimumMovingPercent != settings.minimumMovingPercent)
        {
            BreakQuiet();
            lastSample = 0;
            context = key;
            previous = settings;
            configured = true;
        }
        state.enabled = settings.enabled;
        if (!settings.enabled)
        {
            BreakQuiet();
            state.text = "Low-motion reset disabled.";
            return false;
        }
        if (interrupted)
        {
            BreakQuiet();
            state.text = "Low-motion timer paused: frame held or history restarted.";
            return false;
        }
        if (!sample.valid || sample.paused || !std::isfinite(sample.raw) || sample.raw < 0 ||
            !std::isfinite(sample.measuredAt) || !std::isfinite(now) || now < sample.measuredAt ||
            now - sample.measuredAt > .5)
        {
            BreakQuiet();
            state.text = "Low-motion timer waiting for fresh valid MV.";
            return false;
        }
        if (lastSample == sample.measurements)
            return false;
        lastSample = sample.measurements;
        state.peak = sample.raw;
        state.movingPercent = sample.motionPercent;
        if (!BelowMotionThreshold(settings, sample))
        {
            BreakQuiet();
            state.text = "Motion above threshold; low-motion timer cleared.";
            return false;
        }
        if (quietStart < 0 || sample.measuredAt < lastTime || sample.measuredAt - lastTime > .5)
            quietStart = sample.measuredAt;
        lastTime = sample.measuredAt;
        state.quietSeconds = float(std::max(0., sample.measuredAt - quietStart));
        if (state.quietSeconds >= settings.seconds)
        {
            quietStart = sample.measuredAt;
            state.quietSeconds = 0;
            state.triggered = true;
            ++state.requests;
            state.text = "Private DLSS reset requested; low-motion timer restarted.";
            return true;
        }
        state.text = "Low motion: timing the next history reset.";
        return false;
    }
};
} // namespace DlssNr::LowMotionReset
