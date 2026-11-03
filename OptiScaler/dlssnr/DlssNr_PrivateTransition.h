#pragma once
#include "DlssNr_LowMotionReset.h"

namespace DlssNr::PrivateTransition
{
enum MotionMode : uint32_t
{
    Off,
    Sleep,
    Background
};

struct Settings
{
    uint32_t frames;
    float seconds;
    bool timed;
    MotionMode mode;
};

template <class C> Settings Read(const C& c)
{
    const auto seconds = c.DlssNrSuperiorFadeSeconds.value_or_default();
    const auto mode = c.DlssNrSuperiorMotionMode.value_or_default();
    return { std::min(c.DlssNrSuperiorFadeFrames.value_or_default(), 240u),
             std::isfinite(seconds) ? std::clamp(seconds, 0.f, 10.f) : .15f,
             c.DlssNrSuperiorFadeByTime.value_or_default(), mode <= Background ? MotionMode(mode) : Off };
}

struct Fade
{
    float value = 1.f;
    double previous = -1.;
    float Advance(bool visible, const Settings& s, double now)
    {
        const double dt = previous >= 0. ? std::max(0., now - previous) : 0.;
        previous = now;
        const float step =
            s.timed ? (s.seconds > 0.f ? float(dt / s.seconds) : 1.f) : (s.frames ? 1.f / float(s.frames) : 1.f);
        // Snap at the endpoints despite FP32 roundoff in frame-count mode.
        value = std::clamp(value + (visible ? step : -step), 0.f, 1.f);
        if (value < 1e-6f)
            value = 0.f;
        if (value > 1.f - 1e-6f)
            value = 1.f;
        return value;
    }
};

struct Status
{
    float contribution = 1.f, peak = 0.f, quietSeconds = 0.f;
    bool sleeping = false, background = false;
};

struct Motion
{
    LowMotionReset::Controller quiet;
    Fade fade;
    bool hiding = false;
    bool sleeping = false;

    float Update(const Settings& settings, LowMotionReset::Settings gate, const GainPreview::Status& sample, double now,
                 uint64_t context, bool hold)
    {
        gate.enabled = settings.mode != Off;
        gate = LowMotionReset::Sanitize(gate);
        const bool low = gate.enabled && !hold && sample.valid && !sample.paused && std::isfinite(sample.raw) &&
                         sample.raw >= 0.f && sample.raw <= gate.threshold && now >= sample.measuredAt &&
                         now - sample.measuredAt <= .5;
        if (!low)
            hiding = false;
        if (quiet.Update(gate, sample, now, context, hold, false))
            hiding = true;
        if (settings.mode == Off || (sleeping && !hiding))
            fade.value = 1.f; // first evaluation after sleep uses a fresh DLSS history, without a software fade-in
        const float mix = fade.Advance(!hiding, settings, now);
        sleeping = settings.mode == Sleep && hiding && mix == 0.f;
        return mix;
    }
};
} // namespace DlssNr::PrivateTransition
