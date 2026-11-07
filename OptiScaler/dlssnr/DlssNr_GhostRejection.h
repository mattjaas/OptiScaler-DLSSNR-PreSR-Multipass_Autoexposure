#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DlssNr::GhostRejection
{
struct Settings
{
    bool enabled = false;
    float threshold = .01f, softness = .02f, strength = 1.f;
    bool showMask = false;
    uint32_t method = 0; // 0 legacy, 1 spatial depth/MV, 2 temporal disocclusion window, 3 compact mixed precision, 4
                         // compact fast blend, 5 quarter-grid soft dilation
    float radius = 192.f, depthRatio = 1.2f, motionGate = 2.f;
    uint32_t historyFrames = 16, historyFade = 3;
    float historyDepth = .05f;
    float softExpansion = .5f;
    uint32_t coarseRadius = 1;
    bool contrastEnabled = false;
    float contrastThreshold = .02f, contrastSoftness = .005f;
    uint32_t contrastMetric = 0; // 0 maximum RGB difference; 1 signed edit brightness (Rec.709)
    bool outwardFeather = false;
    float outwardRadius = 4.f; // 1080p-height reference pixels, applied after contrast cancellation
    uint32_t outwardMethod = 0, outwardLayers = 1; // 0 soft reconstruction, 1 coarse boundary rings (method 6)
    bool outputGate = false;
    float outputThreshold = .1f, outputSoftness = .05f;
};
inline Settings Sanitize(Settings s)
{
    s.method = s.method <= 5 ? s.method : 2;
    s.threshold = std::isfinite(s.threshold) ? std::clamp(s.threshold, 0.f, .25f) : s.method == 1 ? .004f : .01f;
    s.softness = std::isfinite(s.softness) ? std::clamp(s.softness, .0001f, .25f) : s.method == 1 ? .01f : .02f;
    s.strength = std::isfinite(s.strength) ? std::clamp(s.strength, 0.f, 1.f) : 1.f;
    s.radius = std::isfinite(s.radius) ? std::clamp(s.radius, 8.f, 512.f) : 192.f;
    s.depthRatio = std::isfinite(s.depthRatio) ? std::clamp(s.depthRatio, 1.02f, 4.f) : 1.2f;
    s.motionGate = std::isfinite(s.motionGate) ? std::clamp(s.motionGate, .1f, 16.f) : 2.f;
    s.historyFrames = std::clamp(s.historyFrames, 1u, 120u);
    s.historyFade = std::min(s.historyFade, s.historyFrames);
    s.historyDepth = std::isfinite(s.historyDepth) ? std::clamp(s.historyDepth, .001f, .5f) : .05f;
    s.coarseRadius = std::clamp(s.coarseRadius, 1u, 2u);
    s.softExpansion = std::isfinite(s.softExpansion) ? std::clamp(s.softExpansion, 0.f, 1.f) : .5f;
    s.contrastMetric = s.contrastMetric <= 1u ? s.contrastMetric : 0u;
    s.contrastThreshold = std::isfinite(s.contrastThreshold) ? std::clamp(s.contrastThreshold, 0.f, 1.f) : .02f;
    s.contrastSoftness = std::isfinite(s.contrastSoftness) ? std::clamp(s.contrastSoftness, 0.f, 1.f) : .005f;
    s.outwardRadius = std::isfinite(s.outwardRadius) ? std::clamp(s.outwardRadius, .25f, 32.f) : 4.f;
    s.outwardMethod = s.outwardMethod == 1 && s.method == 5 ? 1u : 0u;
    s.outwardLayers = std::clamp(s.outwardLayers, 1u, 3u);
    s.outputThreshold = std::isfinite(s.outputThreshold) ? std::clamp(s.outputThreshold, 0.f, 1.f) : .1f;
    s.outputSoftness = std::isfinite(s.outputSoftness) ? std::clamp(s.outputSoftness, 0.f, 1.f) : .05f;
    return s;
}
inline bool NeedsEditReference(const Settings& s) { return s.contrastEnabled || s.outputGate; }
template <class Config> Settings Read(const Config& c)
{
    auto s = Sanitize({ c.DlssNrSuperiorGhostRejection.value_or_default(),
                        c.DlssNrSuperiorGhostThreshold.value_or_default(),
                        c.DlssNrSuperiorGhostSoftness.value_or_default(),
                        c.DlssNrSuperiorGhostStrength.value_or_default(),
                        c.DlssNrSuperiorShowGhostMask.value_or_default(),
                        c.DlssNrSuperiorGhostMethod.value_or_default(),
                        c.DlssNrSuperiorGhostRadius.value_or_default(),
                        c.DlssNrSuperiorGhostDepthRatio.value_or_default(),
                        c.DlssNrSuperiorGhostMotionGate.value_or_default(),
                        c.DlssNrSuperiorGhostHistoryFrames.value_or_default(),
                        c.DlssNrSuperiorGhostHistoryFade.value_or_default(),
                        c.DlssNrSuperiorGhostHistoryDepth.value_or_default(),
                        c.DlssNrSuperiorGhostSoftExpansion.value_or_default(),
                        c.DlssNrSuperiorGhostCoarseRadius.value_or_default(),
                        c.DlssNrSuperiorGhostContrast.value_or_default(),
                        c.DlssNrSuperiorGhostContrastThreshold.value_or_default(),
                        c.DlssNrSuperiorGhostContrastSoftness.value_or_default(),
                        c.DlssNrSuperiorGhostContrastMetric.value_or_default(),
                        c.DlssNrSuperiorGhostOutwardFeather.value_or_default(),
                        c.DlssNrSuperiorGhostOutwardRadius.value_or_default(),
                        c.DlssNrSuperiorGhostOutwardMethod.value_or_default(),
                        c.DlssNrSuperiorGhostOutwardLayers.value_or_default(),
                        c.DlssNrSuperiorGhostOutputGate.value_or_default(),
                        c.DlssNrSuperiorGhostOutputThreshold.value_or_default(),
                        c.DlssNrSuperiorGhostOutputSoftness.value_or_default() });
    if (s.method == 1)
    {
        s.threshold = c.DlssNrSuperiorGhostGuidedThreshold.value_or_default();
        s.softness = c.DlssNrSuperiorGhostGuidedSoftness.value_or_default();
    }
    return Sanitize(s);
}
} // namespace DlssNr::GhostRejection
