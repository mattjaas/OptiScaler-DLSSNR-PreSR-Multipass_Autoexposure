#pragma once
#include "DlssNr_GainPreview.h"
#include "DlssNr_PrivateExposure.h"

namespace DlssNr::AutoGain
{
struct Status
{
    bool enabled = false, controlled = false;
    float applied = 1.f;
    uint64_t changes = 0;
    std::string text = "Automatic multiplier disabled.";
};
// CPU-only actuator. The saved manual preference is never overwritten.
// Every actual gain change still resets the private DLSS history in Stabilize.
class Controller
{
    const void* device = nullptr;
    uint64_t context = 0, lastMeasurement = 0;
    double lastChange = 0;
    bool haveContext = false;

  public:
    static constexpr double Cooldown = .5;
    static constexpr uint64_t WarmupMeasurements = 4;
    Status state;
    void Reset() { *this = {}; }
    float Update(bool enabled, bool supported, bool hold, float manual, const GainPreview::Status& estimate,
                 const void* currentDevice, uint64_t currentContext, double now)
    {
        manual = PrivateExposure::PositiveGain(manual);
        if (!enabled || !supported)
        {
            if (haveContext || state.controlled || state.enabled != enabled)
                Reset();
            state.enabled = enabled;
            state.applied = enabled ? std::max(1.f, manual) : manual;
            state.text = enabled ? "Automatic multiplier paused: unsupported input; manual value with 1x minimum."
                                 : "Automatic multiplier disabled.";
            return state.applied;
        }
        if (!haveContext || device != currentDevice || context != currentContext)
        {
            Reset();
            haveContext = true;
            device = currentDevice;
            context = currentContext;
            state.applied = std::max(1.f, manual);
        }
        state.enabled = true;
        if (!state.controlled)
            state.applied = std::max(1.f, manual);
        if (estimate.measurements < lastMeasurement)
            lastMeasurement = 0;
        const bool fresh = estimate.valid && !estimate.paused && std::isfinite(now) &&
                           std::isfinite(estimate.measuredAt) && now >= estimate.measuredAt &&
                           now - estimate.measuredAt <= 1. && std::isfinite(estimate.smoothed) &&
                           estimate.smoothed >= .01f && estimate.smoothed <= 1024.f;
        if (hold || !fresh || estimate.measurements < WarmupMeasurements)
        {
            state.text =
                state.controlled
                    ? "Automatic multiplier held: waiting for usable moving-frame measurements."
                    : "Automatic multiplier waiting for four usable measurements; manual value with 1x minimum.";
            return state.applied;
        }
        if (estimate.measurements == lastMeasurement)
            return state.applied;
        lastMeasurement = estimate.measurements;
        const float target = std::max(1.f, estimate.smoothed);
        // Asymmetric hysteresis: follow increases sooner; avoid oscillating down/up.
        const bool outsideBand = target >= double(state.applied) * 1.02 || target <= double(state.applied) / 1.05;
        if (!state.controlled || (outsideBand && now - lastChange >= Cooldown))
        {
            if (target != state.applied)
            {
                ++state.changes;
                lastChange = now;
            }
            state.applied = target;
            state.controlled = true;
            state.text = "Automatically applying the smoothed suggestion.";
        }
        else
            state.text = "Automatic multiplier held: hysteresis or update interval.";
        return state.applied;
    }
};
} // namespace DlssNr::AutoGain
