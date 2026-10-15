#pragma once
#include <Config.h>
#include "DlssNr_ModelParameters.h"
#include "DlssNr_GuidedShaping.h"
#include "DlssNr_InterPassPolicy.h"
#include "DlssNr_SuperiorScaling.h"
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
    if (!SuperiorScaling::Enabled(cfg) && cfg.DlssNrExperimentScaleAwareStructure.value_or_default() && result.style < 2u)
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

    if (SuperiorScaling::Enabled(cfg))
        return { SuperiorScaling::High(style), SuperiorScaling::Low(style), true };

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

inline GuidedResidualGains EffectiveGuidedResidualGainsForPass(const Config& cfg, uint32_t shapingMode,
                                                               uint32_t transfer, unsigned int pass, float workScale)
{
    // Use each pass's configured style/Structure/Intensity (including its override). Calibration is
    // based on the user setting, before the separate experimental scale-aware model compensation.
    const auto settings = BasePassSettings(cfg, pass);
    auto gains = GuidedResidualBaseGains(cfg, shapingMode, transfer, settings.style);
    if (!gains.active)
        return gains;

    const float modelFactor = ModelGainFactor(settings.localStructure, settings.intensity);
    gains.high = StrengthAdjustedGain(GuidedResidualGainForScale(gains.high, workScale), modelFactor);
    gains.low = StrengthAdjustedGain(GuidedResidualGainForScale(gains.low, workScale), modelFactor);
    return gains;
}

inline GuidedResidualGains EffectiveGuidedResidualGainsForBlock(const Config& cfg, uint32_t shapingMode,
                                                                uint32_t transfer, unsigned int firstPass,
                                                                unsigned int completedPasses, float workScale)
{
    if (completedPasses <= firstPass)
        return {};
    // High/mid retains its compounding policy; low averages the effective gains in this block.
    auto gains = EffectiveGuidedResidualGainsForPass(cfg, shapingMode, transfer, completedPasses - 1u, workScale);
    if (!gains.active || completedPasses - firstPass == 1u)
        return gains;
    gains.high = ProductGuidedHigh(firstPass, completedPasses,
                                  (SuperiorScaling::Enabled(cfg) || cfg.DlssNrGuidedResidualCompoundPasses.value_or_default()),
                                  [&](unsigned pass) {
                                      return EffectiveGuidedResidualGainsForPass(cfg, shapingMode, transfer, pass,
                                                                                workScale).high;
                                  });
    gains.low = MeanGuidedLow(firstPass, completedPasses,
                             [&](unsigned pass) {
                                 return EffectiveGuidedResidualGainsForPass(cfg, shapingMode, transfer, pass,
                                                                           workScale).low;
                             });
    return gains;
}

inline GuidedResidualGains EffectiveGuidedResidualGainsForInterPass(const Config& cfg, uint32_t shapingMode,
                                                                    unsigned int firstPass,
                                                                    unsigned int completedPasses, float workScale)
{
    // Signed working-space residual uses the plain P100-guided calibration, as before.
    auto gains = EffectiveGuidedResidualGainsForBlock(cfg, shapingMode, 7u, firstPass, completedPasses, workScale);
    if (!SuperiorScaling::Enabled(cfg) && cfg.DlssNrInterPassNeutralLow.value_or_default())
        gains.low = 1.0f;
    return gains;
}

inline GuidedResidualGains EffectiveGuidedResidualGainsForResolve(const Config& cfg, uint32_t shapingMode,
                                                                  uint32_t transfer, unsigned int firstPass,
                                                                  unsigned int completedPasses, float workScale)
{
    auto gains = EffectiveGuidedResidualGainsForBlock(cfg, shapingMode, transfer, firstPass, completedPasses, workScale);
    // Neutral inter-pass low consumes no low shaping. Final low therefore averages the entire chain,
    // while final high/mid still covers only models after the last successful inter-pass correction.
    if (gains.active && !SuperiorScaling::Enabled(cfg) && cfg.DlssNrInterPassNeutralLow.value_or_default())
        gains.low = MeanGuidedLow(0u, completedPasses,
                                 [&](unsigned pass) {
                                     return EffectiveGuidedResidualGainsForPass(cfg, shapingMode, transfer, pass,
                                                                               workScale).low;
                                 });
    return gains;
}

inline InterPass::GainSchedule BuildInterPassSchedule(const Config& cfg, uint32_t mode,
                                                      float workScale, unsigned passes, bool spatial)
{
    const uint32_t shapingMode = SuperiorScaling::ShapingMode(cfg);
    return InterPass::MakeGainSchedule(passes, InterPass::Eligible(mode, workScale, passes, spatial) && shapingMode != 0u,
        (SuperiorScaling::Enabled(cfg) || cfg.DlssNrGuidedResidualCompoundPasses.value_or_default()),
        (!SuperiorScaling::Enabled(cfg) && cfg.DlssNrInterPassNeutralLow.value_or_default()), [&](unsigned pass) {
            // Same signed working-space calibration as inter-pass shaping. Do not force low=1
            // here: prediction concerns the block without correction, not the correction itself.
            return EffectiveGuidedResidualGainsForPass(cfg, shapingMode, 7u, pass, workScale);
        });
}
} // namespace DlssNr::Profiles
