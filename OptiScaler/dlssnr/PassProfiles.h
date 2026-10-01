#pragma once
#include <Config.h>
#include "DlssNr_ModelParameters.h"
#include <algorithm>
#include <cmath>

namespace DlssNr::Profiles
{
inline ModelSettings BasePassSettings(const Config& cfg, unsigned int pass)
{
    ModelSettings result { cfg.DlssNrPreset.value_or_default(),
                           cfg.DlssNrStyle.value_or_default(),
                           cfg.DlssNrIntensity.value_or_default(),
                           cfg.DlssNrLocalStructure.value_or_default(),
                           pass == 0 ? cfg.DlssNrLocalTone.value_or_default() : 0.0f,
                           cfg.DlssNrSkinStructure.value_or_default(),
                           cfg.DlssNrAutoMask.value_or_default() };
    if (pass > 0 && pass <= std::size(cfg.DlssNrPassOverrides))
    {
        const auto& extra = cfg.DlssNrPassOverrides[pass - 1];
        if (pass <= 2) // Only the legacy pass 2/3 keys have a preset override.
            result.preset = extra.preset.value_or(result.preset);
        result.style = extra.style.value_or(result.style);
        result.intensity = extra.intensity.value_or(result.intensity);
        result.localStructure = extra.structure.value_or(result.localStructure);
        result.localTone = extra.tone.value_or(result.localTone);
        result.skinStructure = extra.skin.value_or(result.skinStructure);
        result.autoMask = extra.autoMask.value_or(result.autoMask);
    }
    result.preset = std::min(result.preset, 3u);
    result.style = std::min(result.style, 2u);
    const auto bounded = [](float value, float fallback, float minimum)
    { return std::isfinite(value) ? std::clamp(value, minimum, 2.0f) : fallback; };
    result.intensity = bounded(result.intensity, 1.0f, 0.0f);
    result.localStructure = bounded(result.localStructure, 1.0f, 0.0f);
    result.localTone = bounded(result.localTone, pass == 0 ? 1.0f : 0.0f, 0.0f);
    result.skinStructure = bounded(result.skinStructure, -1.0f, -1.0f);
    return result;
}

inline ModelSettings PassSettings(const Config& cfg, unsigned int pass)
{
    ModelSettings result = BasePassSettings(cfg, pass);

    // Experimental scale-aware compensation: Standard/Natural can draw coarse model-space structure
    // that becomes visually too thick after a strong P50/P60 -> P100 enlargement. Keep P100 exactly
    // unchanged, interpolate to the user-selected factor at P50, and leave Cinematic untouched.
    if (cfg.DlssNrExperimentScaleAwareStructure.value_or_default() && result.style < 2u)
    {
        const float scale = std::clamp(cfg.DlssNrWorkingScale.value_or_default(), 0.25f, 1.0f);
        const float configuredP50 = cfg.DlssNrExperimentStructureP50Factor.value_or_default();
        const float p50 = std::isfinite(configuredP50) ? configuredP50 : 0.60f;
        const float t = std::clamp((1.0f - scale) / 0.5f, 0.0f, 1.0f);
        const float factor = std::lerp(1.0f, p50, t);
        result.localStructure *= factor;
        if (result.skinStructure >= 0.0f)
            result.skinStructure *= factor;
    }
    return result;
}

struct GuidedResidualGains
{
    float high = 1.0f;
    float low = 1.0f;
    bool active = false;
};

inline float GuidedResidualGainForScale(float p50Gain, float workScale)
{
    if (!std::isfinite(p50Gain))
        return 1.0f;

    // Geometric resolution correction anchored at P50:
    // gain(scale) = gain50 ^ log2(1 / scale)
    // 0.90 @ P100/P50/P25 => 1.00 / 0.90 / 0.81.
    const float scale = std::clamp(workScale, 0.25f, 1.0f);
    const float exponent = std::log2(1.0f / scale);
    const float gain = std::pow(std::max(0.0f, p50Gain), exponent);
    return std::isfinite(gain) ? gain : 1.0f;
}

inline GuidedResidualGains GuidedResidualBaseGains(const Config& cfg, uint32_t shapingMode,
                                                   uint32_t transfer, uint32_t style)
{
    GuidedResidualGains gains {};
    style = std::min(style, 2u);

    if (shapingMode == 0u)
        return gains;

    if (shapingMode == 1u)
    {
        // Capture-calibrated Auto currently exists only for the plain P100-guided path.
        // Temporal DLAA modes stay neutral until separately fitted against true NR100.
        if (transfer != 7u)
            return gains;
        gains.active = true;
        if (style == 0u)
        {
            gains.high = 0.421f;
            gains.low = 0.609f;
        }
        else if (style == 1u)
        {
            gains.high = 0.454f;
            gains.low = 0.741f;
        }
        else
        {
            gains.high = 0.370f;
            gains.low = 0.780f;
        }
        return gains;
    }

    gains.active = true;
    if (style == 0u)
    {
        gains.high = cfg.DlssNrGuidedResidualStandardHighGain.value_or_default();
        gains.low = cfg.DlssNrGuidedResidualStandardLowGain.value_or_default();
    }
    else if (style == 1u)
    {
        gains.high = cfg.DlssNrGuidedResidualHighGain.value_or_default();
        gains.low = cfg.DlssNrGuidedResidualLowGain.value_or_default();
    }
    else
    {
        gains.high = cfg.DlssNrGuidedResidualCinematicHighGain.value_or_default();
        gains.low = cfg.DlssNrGuidedResidualCinematicLowGain.value_or_default();
    }

    if (!std::isfinite(gains.high))
        gains.high = style == 0u ? 0.421f : style == 1u ? 0.454f : 0.370f;
    if (!std::isfinite(gains.low))
        gains.low = style == 0u ? 0.609f : style == 1u ? 0.741f : 0.780f;
    return gains;
}

inline GuidedResidualGains EffectiveGuidedResidualGains(const Config& cfg, uint32_t shapingMode,
                                                        uint32_t transfer, uint32_t style,
                                                        unsigned int effectivePasses, float workScale)
{
    auto gains = GuidedResidualBaseGains(cfg, shapingMode, transfer, style);
    if (!gains.active)
        return gains;

    const float singlePassHigh = GuidedResidualGainForScale(gains.high, workScale);
    gains.low = GuidedResidualGainForScale(gains.low, workScale);

    // Optional pass-count compounding applies only to high/mid. When disabled, the gain still follows
    // the working-resolution correction but remains a one-pass value regardless of NR pass count.
    if (cfg.DlssNrGuidedResidualCompoundPasses.value_or_default())
        gains.high = std::pow(singlePassHigh, static_cast<float>(std::max(1u, effectivePasses)));
    else
        gains.high = singlePassHigh;
    if (!std::isfinite(gains.high))
        gains.high = 1.0f;
    return gains;
}

} // namespace DlssNr::Profiles
