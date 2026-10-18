#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DlssNr::SuperiorScaling
{
constexpr uint32_t Transfer = 11u;
constexpr uint32_t ShaderFlag = 65536u;
inline bool Enabled(uint32_t transfer) { return transfer == Transfer; }
template<class C> inline bool Enabled(const C& cfg) { return Enabled(cfg.DlssNrTransfer.value_or_default()); }
inline int Preset(int preset) { return preset == 11 ? 11 : 1; } // K or A; no default/latest ambiguity.
inline float High(unsigned style) { constexpr float v[] { .8f, .75f, .95f }; return v[std::min(style,2u)]; }
inline float Low(unsigned style) { constexpr float v[] { .661f, .787f, .784f }; return v[std::min(style,2u)]; }

// Mode 1 / signed Transfer 9 only. The optimized PSO fixes inspector switches and actual reduction.
template<class C> inline bool NormalResolve(const C& c, uint64_t sourceWidth, uint32_t sourceHeight)
{
    return c.Mode == 1u && c.ResidualHistoryValid == 1u && c.Transfer == 9u &&
           c.DebugView == 0u && c.CompareMode == 0u && c.ShowSkinMask == 0u && c.ApplyModel != 0u &&
           c.DirectResolveFlags == ShaderFlag && sourceWidth != 0u && sourceHeight != 0u &&
           sourceWidth <= c.Width && sourceHeight <= c.Height &&
           (sourceWidth != c.Width || sourceHeight != c.Height);
}
template<class C> inline uint32_t InterPassMode(const C& cfg)
{ return Enabled(cfg) ? 8u : std::min(cfg.DlssNrInterPassReconstruction.value_or_default(),8u); }
template<class C> inline uint32_t DownscaleFilter(const C& cfg)
{ return Enabled(cfg) ? 0u : cfg.DlssNrProxyDownscaleFilter.value_or_default(); }
template<class C> inline uint32_t Radius(const C& cfg)
{ return Enabled(cfg) ? 1u : std::clamp(cfg.DlssNrGuidedResidualRadius.value_or_default(),1u,3u); }
template<class C> inline float RangeSigma(const C& cfg)
{ const float v=Enabled(cfg)? .015f:cfg.DlssNrGuidedResidualRangeSigma.value_or_default(); return std::isfinite(v)?v:.015f; }
template<class C> inline float SpatialSigma(const C& cfg)
{ const float v=Enabled(cfg)?3.0f:cfg.DlssNrGuidedResidualSpatialSigma.value_or_default(); return std::isfinite(v)?v:1.2f; }
template<class C> inline float GuideStrength(const C& cfg)
{ const float v=Enabled(cfg)?1.0f:cfg.DlssNrGuidedResidualGuideStrength.value_or_default(); return std::isfinite(v)?v:.75f; }
template<class C> inline uint32_t ShapingMode(const C& cfg)
{ return Enabled(cfg)?2u:std::min(cfg.DlssNrGuidedResidualShaping.value_or_default(),2u); }
} // namespace DlssNr::SuperiorScaling
