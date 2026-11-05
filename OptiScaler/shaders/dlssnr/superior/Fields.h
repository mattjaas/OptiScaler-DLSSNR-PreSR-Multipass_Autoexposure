#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Constants.h"
#include "../../../dlssnr/DlssNr_DepthTransport.h"
#include "../../../dlssnr/DlssNr_GhostRejection.h"
#include "../DlssNr_Common.h"

// Superior passes reuse constant-buffer fields named for other modes (the v59 shader ABI and the shared resolve's
// cbuffer). Every such reuse goes through one of these named setters, and the offsets the shaders read are pinned
// below, so a layout change fails to compile instead of silently moving a value into another field.
namespace DlssNr::Superior::Fields
{
// Final and inter-pass frequency shaping: the high and low band gains travel in MvScaleX / MvScaleY
// (HostResolve.hlsli: gMvScaleX * edit + (gMvScaleY - gMvScaleX) * low). Both constant structs.
template <class C> inline void SetBandGains(C& c, float high, float low)
{
    c.MvScaleX = high;
    c.MvScaleY = low;
}
template <class C> inline float HighGain(const C& c) { return c.MvScaleX; }
template <class C> inline float LowGain(const C& c) { return c.MvScaleY; }

// Host resolve flags (HostResolve.hlsli): GuideWidth != 0 binds the 1/8 low field at t5 for shaping; GuideHeight != 0
// means the model texture holds a raw NR answer (non-temporal), 0 a decoded signed DLSS residual.
template <class C> inline void SetResolveFlags(C& c, bool lowField, bool rawAnswer)
{
    c.GuideWidth = lowField ? 1u : 0u;
    c.GuideHeight = rawAnswer ? 1u : 0u;
}

// Inter-pass shaping (v59 mode 36): GuideWidth 1 = a low field is bound.
inline void SetInterPassLowField(DlssNrConstants& c, bool lowField) { c.GuideWidth = lowField ? 1u : 0u; }
inline bool InterPassLowField(const DlssNrConstants& c) { return c.GuideWidth != 0u; }

// Private carrier only: WhitePoint is unused by its encode/decode modes.
inline void SetPositiveCarrierGain(DlssNrConstants& c, float gain) { c.WhitePoint = gain; }
// Carrier decode only: the otherwise unused exposure padding stores the raw-edit blend weight as float bits.
inline void SetUnfilteredEditWeight(DlssNrConstants& c, float weight)
{
    std::memcpy(&c.ExposurePadding, &weight, sizeof(weight));
}
inline bool PositiveUnfilteredEditWeight(const DlssNrConstants& c)
{
    float weight;
    std::memcpy(&weight, &c.ExposurePadding, sizeof(weight));
    return std::isfinite(weight) && weight > 0.f;
}
static_assert(offsetof(DlssNrConstants, ExposurePadding) == 172);

// Decode only: select the rejection PSO. Former transport slots 0..3 remain reserved.
inline void SetCarrierGhostRejection(DlssNrConstants& c, DlssNr::GhostRejection::Settings settings)
{
    const auto s = DlssNr::GhostRejection::Sanitize(settings);
    c.GuideWidth = s.enabled && s.strength > 0.f ? 1u : 0u;
    c.GuideHeight = s.method;
    c.ColourStrength = s.threshold;
    c.TransferStrength = s.softness;
    c.CompareSplit = s.strength;
    c.ExposureAnchors[4] = s.radius;
    c.ExposureAnchors[5] = s.depthRatio;
    c.ExposureAnchors[6] = s.motionGate;
    c.ExposureAnchors[8] = float(s.historyFrames);
    c.ExposureAnchors[9] = float(s.historyFade);
    c.ExposureAnchors[10] = s.historyDepth;
    c.ExposureAnchors[11] = s.softExpansion;
    c.ExposureAnchors[12] = s.contrastEnabled && s.method >= 2 ? 1.f : 0.f;
    c.ExposureAnchors[13] = s.contrastThreshold;
    c.ExposureAnchors[14] = s.contrastSoftness;
    c.ExposureAnchors[15] = float(s.contrastMetric);
}
inline bool CarrierGhostRejection(const DlssNrConstants& c) { return c.Mode == 38u && c.GuideWidth == 1u; }
inline bool GuidedGhostRejection(const DlssNrConstants& c) { return CarrierGhostRejection(c) && c.GuideHeight == 1u; }
inline bool TemporalGhostRejection(const DlssNrConstants& c)
{
    return CarrierGhostRejection(c) && (c.GuideHeight >= 2u && c.GuideHeight <= 5u);
}
inline bool CompactGhostRejection(const DlssNrConstants& c)
{
    return CarrierGhostRejection(c) && (c.GuideHeight >= 3u && c.GuideHeight <= 5u);
}
inline void SetGhostDepthPrecision(DlssNrConstants& c, float precision)
{
    c.ExposureAnchors[7] =
        std::isfinite(precision) && precision >= 0.f && precision <= .001f ? precision : 1.f / 65535.f;
}
static_assert(offsetof(DlssNrConstants, ExposureAnchors) + 4 * 4 == 192 &&
              offsetof(DlssNrConstants, GuideHeight) == 48);
// Visibility stages reinterpret the first 52 bytes, matching TemporalGhost.hlsl's Params.
inline void SetGhostVisibility(DlssNrConstants& c, unsigned width, unsigned height, unsigned fullWidth,
                               unsigned fullHeight, DlssNr::GhostRejection::Settings settings, bool inverted,
                               bool previous, float precision)
{
    const auto s = DlssNr::GhostRejection::Sanitize(settings);
    c.Width = width;
    c.Height = height;
    c.GuideWidth = fullWidth;
    c.GuideHeight = fullHeight;
    c.WhitePoint = float(s.historyFrames);
    c.TransferStrength = float(s.historyFade);
    c.ColourStrength = s.historyDepth;
    c.MaxRatio = s.motionGate;
    c.MvScaleY = s.method == 4 ? 1.f : 0.f; // unused visibility cbuffer slot: host PSO selection only
    c.Passthrough = inverted ? 1u : 0u;
    c.DebugView = previous ? 1u : 0u;
    c.MvScaleX = std::isfinite(precision) && precision >= 0 && precision <= .001f ? precision : 1.f / 65535.f;
}
static_assert(offsetof(DlssNrConstants, Width) == 8 && offsetof(DlssNrConstants, Height) == 12 &&
              offsetof(DlssNrConstants, DebugView) == 24 && offsetof(DlssNrConstants, Passthrough) == 32 &&
              offsetof(DlssNrConstants, MvScaleX) == 36);
// Stage 47 only; MvScaleY is the unused visibility slot at byte 40, not an MV multiplier here.
inline bool FastCompactScatter(const DlssNrConstants& c) { return c.Mode == 47u && c.MvScaleY == 1.f; }
static_assert(offsetof(DlssNrConstants, MvScaleY) == 40);
// Optional decode PSOs bind the original occluder edit at t1; fields occupy bytes 224..236.
inline bool GhostEditContrast(const DlssNrConstants& c)
{
    return TemporalGhostRejection(c) && c.ExposureAnchors[12] == 1.f;
}
inline void SetGhostContrastHistory(DlssNrConstants& c, unsigned method, bool previous)
{
    c.Mode = 52;
    c.CompareMode = method;
    c.DebugView = previous ? 1u : 0u;
}
static_assert(offsetof(DlssNrConstants, ExposureAnchors) + 12 * 4 == 224 &&
              offsetof(DlssNrConstants, CompareMode) == 52 &&
              offsetof(DlssNrConstants, ExposureAnchors) + 15 * 4 == 236);
inline void SetCoarseGhostDilate(DlssNrConstants& c, unsigned w, unsigned h,
                                 const DlssNr::GhostRejection::Settings& settings)
{
    const auto s = DlssNr::GhostRejection::Sanitize(settings);
    c.Mode = 49;
    c.Width = w;
    c.Height = h;
    c.TransferStrength = float(s.historyFade);
    c.DebugView = s.coarseRadius;
}
static_assert(offsetof(DlssNrConstants, TransferStrength) == 16 && offsetof(DlssNrConstants, DebugView) == 24);
// Outward feather stages reinterpret bytes 0..31. Radius and depth gate are independent of transport gain.
inline void SetGhostOutwardFeather(DlssNrConstants& c, unsigned w, unsigned h,
                                   DlssNr::GhostRejection::Settings settings, bool inverted, float globalRaw,
                                   float precision, bool apply)
{
    const auto s = DlssNr::GhostRejection::Sanitize(settings);
    c = {};
    c.Mode = apply ? 55u : 54u;
    c.Width = w;
    c.Height = h;
    c.WhitePoint = s.outwardRadius;
    c.TransferStrength = std::isfinite(globalRaw) ? std::clamp(globalRaw, 0.f, 1.f) : 0.f;
    c.ColourStrength = s.historyDepth;
    c.DebugView = inverted ? 1u : 0u;
    c.MaxRatio = std::isfinite(precision) && precision >= 0 && precision <= .001f ? precision : 1.f / 65535.f;
}
static_assert(offsetof(DlssNrConstants, WhitePoint) == 4 && offsetof(DlssNrConstants, ColourStrength) == 20 &&
              offsetof(DlssNrConstants, DebugView) == 24 && offsetof(DlssNrConstants, MaxRatio) == 28);
// Final composition only: bit 0 retains the existing skin-mask preview; bit 1 selects the green ghost overlay PSO.
inline void SetGhostMaskPreview(::DlssNrConstants& c, bool enabled)
{
    c.ShowSkinMask = (c.ShowSkinMask & 1u) | (enabled ? 2u : 0u);
}
inline bool GhostMaskPreview(const ::DlssNrConstants& c) { return (c.ShowSkinMask & 2u) != 0u; }
static_assert(offsetof(::DlssNrConstants, ShowSkinMask) == 96);
static_assert(offsetof(DlssNrConstants, GuideWidth) == 44 && offsetof(DlssNrConstants, ColourStrength) == 20 &&
              offsetof(DlssNrConstants, TransferStrength) == 16 && offsetof(DlssNrConstants, CompareSplit) == 56);

static_assert(offsetof(DlssNrConstants, WhitePoint) == 4);
// Legacy test-only setter: current carrier shaders ignore this obsolete saturation slot.
inline void SetCarrierSaturation(DlssNrConstants& c, float saturation) { c.MaxRatio = saturation; }
static_assert(offsetof(DlssNrConstants, MaxRatio) == 28);
// Carrier only: full NR answer instead of the 0.5 residual; t1 supplies proxy during decode.
inline void SetCarrierNrAnswer(DlssNrConstants& c, bool enabled) { c.DirectDetailMode = enabled ? 1u : 0u; }
static_assert(offsetof(DlssNrConstants, DirectDetailMode) == 240);
// Guided rejection still needs the depth convention, independently of retired colour controls.
inline void SetCarrierDepthConvention(DlssNrConstants& c, bool inverted) { c.Passthrough = inverted ? 1u : 0u; }
// Legacy parameter setters below remain only for stale-field regression and archived capture tests.
inline void SetCarrierDepthColour(DlssNrConstants& c, float strength, float curve, uint32_t pair, bool inverted)
{
    c.ResidualScale = strength;
    c.ResidualBlend = curve;
    c.SkinProtection = pair;
    c.Passthrough = inverted ? 1u : 0u;
}
static_assert(offsetof(DlssNrConstants, ResidualScale) == 88 && offsetof(DlssNrConstants, ResidualBlend) == 116 &&
              offsetof(DlssNrConstants, SkinProtection) == 92);

// Private carrier only; these exposure/resolve fields are not otherwise read.
inline void SetCarrierDepthBrightness(DlssNrConstants& c, const DlssNr::DepthTransport::Settings& value)
{
    const auto s = DlssNr::DepthTransport::Sanitize(value);
    c.DirectResolveUpscaler = DlssNr::DepthTransport::Flags(s);
    c.ExposureAnchors[0] = s.nearLimit;
    c.ExposureAnchors[1] = s.farLimit;
    c.ExposureAnchors[2] = s.curve;
    c.ExposureAnchors[3] = s.bias;
}
static_assert(offsetof(DlssNrConstants, ExposureAnchors) == 176 &&
              offsetof(DlssNrConstants, DirectResolveUpscaler) == 248);

// Experimental exposure modes 40..42 reuse the frozen cbuffer, without changing v59's ABI.
inline void SetPrivateExposure(DlssNrConstants& c, uint32_t method, uint32_t sourceW, uint32_t sourceH, bool measure,
                               bool reset, bool initialized)
{
    c.ExposureMode = method;
    c.ExposureSourceWidth = sourceW;
    c.ExposureSourceHeight = sourceH;
    c.PreExposure = 1.f; // the private carrier has no scene pre-exposure
    c.DebugView = measure ? 1u : 0u;
    c.ApplyModel = reset ? 1u : 0u;
    c.ResidualHistoryValid = initialized ? 1u : 0u;
}
static_assert(offsetof(DlssNrConstants, ExposureMode) == 144 && offsetof(DlssNrConstants, PreExposure) == 148 &&
              offsetof(DlssNrConstants, ExposureSourceWidth) == 164);

// v59 guide resize / carrier modes (10, 39): the depth and motion regions travel in the comparison fields.
//   depth:  size GuideWidth/GuideHeight, origin DebugView/CompareMode
//   motion: size TransferStrength/ColourStrength (as floats), origin CompareSwap/Transfer
inline void SetDepthRegion(DlssNrConstants& c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    c.GuideWidth = w;
    c.GuideHeight = h;
    c.DebugView = x;
    c.CompareMode = y;
}
inline void SetMotionRegion(DlssNrConstants& c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    c.TransferStrength = float(w);
    c.ColourStrength = float(h);
    c.CompareSwap = x;
    c.Transfer = y;
}

// The fields above, at the offsets both cbuffers (superior/precompile/dlssnr.hlsl and precompile/dlssnr.hlsl,
// "cbuffer Params") declare them. Changing either struct or cbuffer requires changing these and rebuilding blobs.
#define DLSSNR_SUPERIOR_PIN(Struct)                                                                                    \
    static_assert(offsetof(Struct, Mode) == 0 && offsetof(Struct, Width) == 8 && offsetof(Struct, Height) == 12);     \
    static_assert(offsetof(Struct, TransferStrength) == 16 && offsetof(Struct, ColourStrength) == 20);              \
    static_assert(offsetof(Struct, DebugView) == 24 && offsetof(Struct, Passthrough) == 32);                        \
    static_assert(offsetof(Struct, MvScaleX) == 36 && offsetof(Struct, MvScaleY) == 40);                            \
    static_assert(offsetof(Struct, GuideWidth) == 44 && offsetof(Struct, GuideHeight) == 48);                       \
    static_assert(offsetof(Struct, CompareMode) == 52 && offsetof(Struct, CompareSwap) == 64);                      \
    static_assert(offsetof(Struct, Transfer) == 68 && sizeof(Struct) == 256);
DLSSNR_SUPERIOR_PIN(DlssNr::Superior::DlssNrConstants)
DLSSNR_SUPERIOR_PIN(::DlssNrConstants)
#undef DLSSNR_SUPERIOR_PIN
} // namespace DlssNr::Superior::Fields
