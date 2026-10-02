#include "pch.h"
#include "DlssNr_MenuSections.h"
#include "DlssNr_Placement.h"
#include "DlssNr_Status.h"
#include "PassProfiles.h"
#include <shaders/dlssnr/DlssNr_Spatial.h>
#include <Config.h>
#include <menu/menu_common.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>

namespace DlssNr::MenuSections
{
template <typename Option> static void Checkbox(const char* label, Option& option)
{
    bool value = option.value_or_default();
    if (ImGui::Checkbox(label, &value))
        option = value;
}

template <typename Option>
static void Slider(const char* label, Option& option, float minimum, float maximum, const char* format = "%.2f",
                   std::optional<float> reset = {}, ImGuiSliderFlags flags = 0)
{
    float value = option.value_or_default();
    if (ImGui::SliderFloat(label, &value, minimum, maximum, format, flags))
        option = value;
    if (reset)
    {
        ImGui::SameLine();
        ImGui::PushID(label);
        if (ImGui::SmallButton("Reset"))
            option = *reset;
        ImGui::PopID();
    }
}

static void StoreSpatial(Config* config, const Spatial::Settings& value)
{
    config->DlssNrSpatialCenterX = value.centerX;
    config->DlssNrSpatialCenterY = value.centerY;
    config->DlssNrSpatialWorkX = value.workX;
    config->DlssNrSpatialWorkY = value.workY;
    config->DlssNrSpatialOffsetX = value.offsetX;
    config->DlssNrSpatialOffsetY = value.offsetY;
    config->DlssNrSpatialShiftX = value.shiftX;
    config->DlssNrSpatialShiftY = value.shiftY;
}

// Used only for user edits. Invalid INI values remain visible as a runtime fallback.
static void ConstrainSpatialControls(Spatial::Settings& value, float scale)
{
    const float minimumWork = Spatial::MinimumWorkPercent(scale);
    auto axis = [&](float& center, float& work, float& offset)
    {
        center = std::clamp(std::isfinite(center) ? center : 80.0f, 1.0f, 99.5f);
        work = std::clamp(std::isfinite(work) ? work : 90.0f, std::max(minimumWork, center + 0.5f), 100.0f);
        const float limit = Spatial::MaxCenterOffset(center);
        offset = std::clamp(std::isfinite(offset) ? offset : 0.0f, -limit, limit);
    };
    axis(value.centerX, value.workX, value.offsetX);
    axis(value.centerY, value.workY, value.offsetY);
    const auto x = Spatial::WorkShiftLimits(value, false);
    const auto y = Spatial::WorkShiftLimits(value, true);
    value.shiftX = std::clamp(std::isfinite(value.shiftX) ? value.shiftX : 0.0f, x.first, x.second);
    value.shiftY = std::clamp(std::isfinite(value.shiftY) ? value.shiftY : 0.0f, y.first, y.second);
}

static void RenderSpatial(Config* config)
{
    Checkbox("Peripheral compression", config->DlssNrSpatialCompression);
    HelpMarker(
        "Keep more model detail in the centre and compress the edges. Model resolution still scales the whole image.");
    bool preview = config->DlssNrDebugView.value_or_default() == 4;
    if (ImGui::Checkbox("Preview", &preview))
        config->DlssNrDebugView = preview ? 4u : 0u;
    HelpMarker("Show the packed model input before spatial unpacking, scaled to fill the screen. "
               "This is the same as the Compressed model input debug view. "
               "Without active compression, shows the ordinary model input. Apply model must be enabled.");
    if (!config->DlssNrSpatialCompression.value_or_default())
        return;

    const auto feature = State::Instance().currentFeature;
    const bool nativeVk = feature && feature->Api() == API::Vulkan && !feature->IsWithDx12();
    const auto status = ReadStatus(nativeVk ? Backend::Vulkan : Backend::Dx12);
    if (!status.spatialStatus.empty())
        ImGui::TextWrapped("%s", status.spatialStatus.c_str());

    static Spatial::Settings pending;
    static bool editing = false;
    if (!editing)
        pending = Spatial::ReadSettings(*config);
    const float scale = config->DlssNrWorkingScale.value_or_default();
    ConstrainSpatialControls(pending, scale);
    bool commit = false;
    auto slider = [&](const char* label, float& value, float lo, float hi)
    {
        if (ImGui::SliderFloat(label, &value, lo, hi, "%.1f%%"))
            editing = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;
    };
    slider("Centre width", pending.centerX, 1.0f, pending.workX - 0.5f);
    slider("Centre height", pending.centerY, 1.0f, pending.workY - 0.5f);
    const float minimumWork = Spatial::MinimumWorkPercent(scale);
    slider("Working width", pending.workX, std::max(minimumWork, pending.centerX + 0.5f), 100.0f);
    slider("Working height", pending.workY, std::max(minimumWork, pending.centerY + 0.5f), 100.0f);
    const float xLimit = Spatial::MaxCenterOffset(pending.centerX);
    const float yLimit = Spatial::MaxCenterOffset(pending.centerY);
    slider("Centre horizontal offset", pending.offsetX, -xLimit, xLimit);
    slider("Centre vertical offset", pending.offsetY, -yLimit, yLimit);
    const auto xShift = Spatial::WorkShiftLimits(pending, false);
    const auto yShift = Spatial::WorkShiftLimits(pending, true);
    slider("Working region horizontal shift", pending.shiftX, xShift.first, xShift.second);
    slider("Working region vertical shift", pending.shiftY, yShift.first, yShift.second);
    HelpMarker("Extreme shifts can leave an edge with less than one working pixel. Compression then falls back to "
               "ordinary NR; the status above explains why.");
    if (ImGui::SmallButton("Reset compression layout"))
    {
        pending = Spatial::Settings {};
        commit = true;
    }
    if (commit)
    {
        ConstrainSpatialControls(pending, scale);
        StoreSpatial(config, pending);
        editing = false;
    }
    Checkbox("Show centre outline", config->DlssNrSpatialShowCenter);
    Checkbox("Show working region outline", config->DlssNrSpatialShowWork);
    ImGui::TextWrapped("Centre detail follows Model resolution. Strong edge compression can soften detail or shimmer "
                       "during movement.");
}

void RenderInput(Config* config)
{
    // Resolution changes rebuild model resources; commit only after releasing the slider.
    static int pendingScale = -1;

    int scalePercent =
        pendingScale >= 0 ? pendingScale : (int) lroundf(config->DlssNrWorkingScale.value_or_default() * 100.0f);

    if (ImGui::SliderInt("Model resolution", &scalePercent, 25, 200, "%d%%"))
        pendingScale = scalePercent;

    if (ImGui::IsItemDeactivatedAfterEdit() && pendingScale >= 0)
    {
        config->DlssNrWorkingScale = std::clamp(pendingScale, 25, 200) / 100.0f;
        if (config->DlssNrSpatialCompression.value_or_default())
        {
            auto spatial = Spatial::ReadSettings(*config);
            ConstrainSpatialControls(spatial, config->DlssNrWorkingScale.value_or_default());
            StoreSpatial(config, spatial);
        }
        pendingScale = -1;
    }

    HelpMarker("50% halves width and height. 100% uses the full input size.");
    RenderSpatial(config);

    if (scalePercent < 100 && !config->DlssNrSpatialCompression.value_or_default())
    {
        static const char* proxyDownNames[] = { "Area (default)", "Bilinear", "Catmull-Rom", "Lanczos2",
                                                "Point / nearest", "FSR1", "Bicubic", "Lanczos3", "Kaiser2",
                                                "Kaiser3", "MAGIC", "SSIM Sharp (experimental)" };
        int filter = (int) std::min(config->DlssNrProxyDownscaleFilter.value_or_default(), 11u);
        if (ImGui::Combo("Proxy downscale filter", &filter, proxyDownNames, IM_ARRAYSIZE(proxyDownNames)))
            config->DlssNrProxyDownscaleFilter = (uint32_t) filter;

        HelpMarker("Filter used only to shrink the full-resolution proxy before NR below 100%. "
                   "FSR1, Bicubic, Catmull-Rom, Lanczos2/3, Kaiser2/3 and MAGIC reuse the exact existing "
                   "Output Scaling downscalers. Area is the default; Bilinear and Point are "
                   "simple references. SSIM Sharp is an experimental local structural-contrast variant tuned "
                   "to keep more high-frequency structure. Spatial compression is unaffected.");
    }

    if (scalePercent > 100)
    {
        static const char* dsNames[] = { "FSR1",     "Bicubic", "Catmull-Rom", "Lanczos2",
                                         "Lanczos3", "Kaiser2", "Kaiser3",     "MAGIC" };
        int ds = (int) config->DlssNrScalingDownscaler.value_or_default();
        if (ds < 0 || ds >= IM_ARRAYSIZE(dsNames))
            ds = (int) Scaler::Lanczos3;

        if (ImGui::Combo("Downscaler (NR)", &ds, dsNames, IM_ARRAYSIZE(dsNames)))
            config->DlssNrScalingDownscaler = (Scaler) ds;

        HelpMarker("Downsampling filter for resolutions above 100%.");
    }
    {
        const bool reduced = config->DlssNrWorkingScale.value_or_default() < 0.999f;

        ImGui::BeginDisabled(!reduced);

        static const char* enlargeNames[] = { "Classic", "Matched residual", "Matched residual + DLSS",
                                              "Lighting + colour", "Lighting + colour + DLSS", "Direct NR",
                                              "Upscaled NR residual", "P100-guided residual",
                                              "Temporal DLAA NR50 + P100-guided",
                                              "Temporal DLAA residual + P100-guided",
                                              "Temporal image-anchored residual + P100-guided" };
        int enlarge = (int) std::min(config->DlssNrTransfer.value_or_default(), 10u);

        if (ImGui::Combo("Enlargement", &enlarge, enlargeNames, IM_ARRAYSIZE(enlargeNames)))
            config->DlssNrTransfer = (uint32_t) enlarge;

        ImGui::EndDisabled();

        HelpMarker("Below 100%: Lighting + colour resizes lighting gain and colour changes separately. Direct NR "
                   "enlarges the NR answer itself and composes it like a native 100% model result. Upscaled NR "
                   "residual constructs two P100 images and subtracts them. P100-guided residual instead upsamples "
                   "NR50-P50 directly inside final resolve: untouched native P100 supplies edge-aware positive weights, "
                   "so residual should cross real object boundaries less readily and cannot ring from negative kernel lobes. "
                   "Temporal DLAA NR50 runs private DLSS at the working resolution 1:1 before the same P100-guided resolve. "
                   "Temporal DLAA residual encodes NR50-P50 around neutral 0.5 before 1:1 DLAA. Image-anchored residual "
                   "feeds a compressed clean anchor 0.5+B*(Proxy50-0.5) plus K*(NR50-P50), reserving carrier headroom while "
                   "DLSS still sees scene structure; optional Paired DLAA subtracts a second independently temporalized clean "
                   "anchor before P100 guidance. All temporal modes keep jitter at zero because this "
                   "post-upscale colour has already been reconstructed by the game's upscaler; resized depth/MV still feed "
                   "the private DLSS history. DLSS-based modes require post-upscale DX12 processing.");

        const auto transfer = config->DlssNrTransfer.value_or_default();

        if (reduced)
        {
            const bool styleCaptureSupported = !config->DlssNrSpatialCompression.value_or_default();
            ImGui::BeginDisabled(!styleCaptureSupported);
            if (ImGui::Button("Capture P100/P50 + Standard/Natural/Cinematic at P50 and P100"))
                DlssNr::RequestStyleAnalysisCapture();
            ImGui::EndDisabled();
            HelpMarker("One-shot scaling-analysis capture. Six independent NVIDIA NR features evaluate Standard, "
                       "Natural and Cinematic at the reduced working size and at native P100 on the same frame. "
                       "The P50 styles use the exact sharp reduced proxy before Detail Quality Lab processing; the P100 "
                       "styles use the native proxy. Saves proxy P100/P50 plus NR50 and NR100 outputs as shared-range "
                       "16-bit PNG and exact RAW files under nr-style-analysis-captures.");
        }

        if (reduced && transfer >= 7 && transfer <= 10)
        {
            ImGui::TextUnformatted(transfer == 7 ? "P100-guided residual"
                                   : transfer == 8 ? "Temporal DLAA NR50 + P100-guided"
                                   : transfer == 9 ? "Temporal DLAA residual + P100-guided"
                                                    : "Temporal image-anchored residual + P100-guided");
            if (transfer == 8 || transfer == 9 || transfer == 10)
            {
                static const char* temporalPresetNames[] = { "Default", "A", "B", "C", "D", "E", "F", "G", "H",
                                                             "I", "J", "K", "L", "M", "N", "O", "Latest" };
                static constexpr int temporalPresetValues[] = {
                    NVSDK_NGX_DLSS_Hint_Render_Preset_Default,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_A,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_B,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_C,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_D,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_E,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_F,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_G,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_J,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_K,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_L,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_M,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_N,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_O,
                    static_cast<int>(NV_PRESET_LATEST)
                };
                auto& temporalPresetSetting =
                    transfer == 8 ? config->DlssNrTemporalDlaaNrPreset
                    : transfer == 9 ? config->DlssNrTemporalDlaaResidualPreset
                                     : config->DlssNrTemporalDlaaAnchoredPreset;
                int temporalPresetIndex = 1;
                const int configuredTemporalPreset = temporalPresetSetting.value_or_default();
                for (int i = 0; i < IM_ARRAYSIZE(temporalPresetValues); ++i)
                    if (temporalPresetValues[i] == configuredTemporalPreset)
                        temporalPresetIndex = i;
                const char* temporalPresetLabel =
                    transfer == 8 ? "Temporal DLAA NR preset"
                    : transfer == 9 ? "Temporal DLAA Residual preset"
                                     : "Temporal image-anchored preset";
                if (ImGui::Combo(temporalPresetLabel, &temporalPresetIndex, temporalPresetNames,
                                 IM_ARRAYSIZE(temporalPresetNames)))
                    temporalPresetSetting = temporalPresetValues[temporalPresetIndex];
                HelpMarker("NGX render preset used only by this private 1:1 DLAA feature. Changing it recreates "
                           "the temporal DLSS feature and starts a fresh history.");

                auto& temporalAutoExposureSetting =
                    transfer == 8 ? config->DlssNrTemporalDlaaNrAutoExposure
                    : transfer == 9 ? config->DlssNrTemporalDlaaResidualAutoExposure
                                     : config->DlssNrTemporalDlaaAnchoredAutoExposure;
                bool temporalAutoExposure = temporalAutoExposureSetting.value_or_default();
                if (ImGui::Checkbox("Private DLAA Auto Exposure (experimental)", &temporalAutoExposure))
                    temporalAutoExposureSetting = temporalAutoExposure;
                HelpMarker("Sets NVSDK_NGX_DLSS_Feature_Flags_AutoExposure only for this temporal DLAA mode. "
                           "The image-anchored Paired baseline uses the same setting on both DLAA histories.");

                bool temporalIsHdr = config->DlssNrTemporalDlaaIsHdr.value_or_default();
                if (ImGui::Checkbox("Private DLAA IsHDR (experimental)", &temporalIsHdr))
                    config->DlssNrTemporalDlaaIsHdr = temporalIsHdr;
                HelpMarker("Sets NVSDK_NGX_DLSS_Feature_Flags_IsHDR on the private 1:1 DLAA feature. This is independent "
                           "from the carrier numeric range and keeps pre-exposure/exposure fixed at 1 for an isolated A/B test. "
                           "Changing it recreates the private DLAA history.");
            }
            if (transfer == 9 || transfer == 10)
            {
                if (transfer == 10)
                {
                    static const char* anchorNames[] = { "0.25", "0.50", "0.75", "1.00", "Custom" };
                    const float currentB = config->DlssNrTemporalAnchoredBaseStrength.value_or_default();
                    const int inferredAnchorPreset =
                        std::abs(currentB - 0.25f) < 0.0001f ? 0
                        : std::abs(currentB - 0.50f) < 0.0001f ? 1
                        : std::abs(currentB - 0.75f) < 0.0001f ? 2
                        : std::abs(currentB - 1.00f) < 0.0001f ? 3 : 4;
                    bool anchorCustom = config->DlssNrTemporalAnchoredBaseStrengthCustom.value_or_default();
                    if (inferredAnchorPreset == 4 && !anchorCustom)
                    {
                        // Preserve old configs containing an arbitrary B and migrate them to explicit Custom mode.
                        anchorCustom = true;
                        config->DlssNrTemporalAnchoredBaseStrengthCustom = true;
                    }
                    int anchorPreset = anchorCustom ? 4 : inferredAnchorPreset;
                    if (ImGui::Combo("Anchor strength B", &anchorPreset, anchorNames, IM_ARRAYSIZE(anchorNames)))
                    {
                        static constexpr float anchorValues[] = { 0.25f, 0.50f, 0.75f, 1.00f };
                        if (anchorPreset < 4)
                        {
                            config->DlssNrTemporalAnchoredBaseStrength = anchorValues[anchorPreset];
                            config->DlssNrTemporalAnchoredBaseStrengthCustom = false;
                        }
                        else
                        {
                            config->DlssNrTemporalAnchoredBaseStrengthCustom = true;
                        }
                    }
                    if (anchorPreset == 4)
                        Slider("Custom anchor B", config->DlssNrTemporalAnchoredBaseStrength,
                               0.0f, 1.0f, "%.3f", 0.50f);
                    HelpMarker("Anchor = 0.5 + B*(saturate(Proxy50)-0.5). Lower B reserves more symmetric carrier "
                               "headroom while preserving a lower-contrast copy of scene structure for DLAA.");

                    bool paired = config->DlssNrTemporalAnchoredPairedBaseline.value_or_default();
                    if (ImGui::Checkbox("Paired DLAA baseline (2x DLAA)", &paired))
                        config->DlssNrTemporalAnchoredPairedBaseline = paired;
                    HelpMarker("Reference experiment: a second independent 1:1 DLAA history processes the compressed "
                               "clean anchor. Decode becomes (DLAA(anchor+K*E)-DLAA(anchor))/K, cancelling DLAA changes "
                               "to the base image. This intentionally costs a second DLAA pass and extra history VRAM.");
                }

                if (transfer == 9)
                {
                    static const char* encodingNames[] = { "Nonlinear E/(1+abs(E))", "Linear 0.5 + K*E" };
                    int encoding = (int) std::min(config->DlssNrTemporalResidualEncoding.value_or_default(), 1u);
                    if (ImGui::Combo("Residual carrier encoding", &encoding, encodingNames, IM_ARRAYSIZE(encodingNames)))
                        config->DlssNrTemporalResidualEncoding = (uint32_t) encoding;
                    HelpMarker("Nonlinear is bounded for any finite residual and was the original experiment. Linear is "
                               "amplitude-faithful and exactly reversible while K stays inside the safe carrier range.");
                }

                const bool extendedRelevant =
                    transfer == 10 || config->DlssNrTemporalResidualEncoding.value_or_default() == 1u;
                bool extendedCarrier = config->DlssNrTemporalCarrierExtendedRange.value_or_default();
                ImGui::BeginDisabled(!extendedRelevant);
                if (ImGui::Checkbox("Extended carrier range [-1, 2] (experimental)", &extendedCarrier))
                    config->DlssNrTemporalCarrierExtendedRange = extendedCarrier;
                ImGui::EndDisabled();
                HelpMarker("For Linear residual and image-anchored carriers, expands the encoded domain from [0,1] "
                           "to [-1,2], keeping neutral at 0.5 and increasing symmetric headroom from 0.5 to 1.5. "
                           "Safe K and Auto K use the selected range. The nonlinear residual encoding remains bounded "
                           "inside [0,1], so this option has no effect there.");

                auto& gainModeSetting =
                    transfer == 9 ? config->DlssNrTemporalResidualGainMode : config->DlssNrTemporalAnchoredGainMode;
                auto& manualGainSetting =
                    transfer == 9 ? config->DlssNrTemporalResidualManualGain : config->DlssNrTemporalAnchoredManualGain;
                static const char* gainModeNames[] = { "Manual", "Auto Strict", "Auto Robust" };
                int gainMode = (int) std::min(gainModeSetting.value_or_default(), 2u);
                if (ImGui::Combo("Temporal carrier gain", &gainMode, gainModeNames, IM_ARRAYSIZE(gainModeNames)))
                    gainModeSetting = (uint32_t) gainMode;
                if (gainMode == 0)
                    Slider("Manual K", manualGainSetting, 0.01f, 32.0f, "%.3f", 4.0f);

                Slider("Carrier edge margin", config->DlssNrTemporalCarrierMargin,
                       0.0f, 0.10f, "%.4f", 0.01f);
                Slider("Auto K ceiling", config->DlssNrTemporalCarrierAutoMaxGain,
                       1.0f, 128.0f, "%.2f", 32.0f);
                if (gainMode != 0)
                    Slider("Auto K rise (stops/s)", config->DlssNrTemporalCarrierAutoRiseStopsPerSecond,
                           0.0f, 16.0f, "%.2f", 4.0f);

                bool ignoreWatermarks = config->DlssNrTemporalCarrierIgnoreNvidiaWatermarks.value_or_default();
                if (ImGui::Checkbox("Ignore NVIDIA watermarks in K analysis", &ignoreWatermarks))
                    config->DlssNrTemporalCarrierIgnoreNvidiaWatermarks = ignoreWatermarks;
                if (ignoreWatermarks)
                {
                    Slider("Watermark margin X (px)", config->DlssNrTemporalCarrierWatermarkMarginX,
                           0.0f, 512.0f, "%.0f", 128.0f);
                    Slider("Watermark margin Y (px)", config->DlssNrTemporalCarrierWatermarkMarginY,
                           0.0f, 256.0f, "%.0f", 64.0f);
                }
                HelpMarker("Excludes only a conservative bottom-left NVIDIA diagnostic watermark region from K-safe "
                           "reduction; carrier/DLAA/final pixels remain untouched. The game/SR watermark is modeled as a "
                           "fixed native-output pixel footprint and then scaled by WorkingScale. The NR watermark is modeled "
                           "as a fixed pixel footprint at the NR working resolution. X/Y margins absorb longer text, small "
                           "position changes and perspective/crop differences between games.");

                const auto carrierStatus = ReadStatus(Backend::Dx12);
                if (carrierStatus.temporalCarrierTelemetryValid)
                {
                    ImGui::Text("Applied K: %.4f   Safe K: %.4f",
                                carrierStatus.temporalCarrierAppliedK, carrierStatus.temporalCarrierSafeK);
                    ImGui::TextDisabled("Min white headroom %.5f   Min black headroom %.5f",
                                        carrierStatus.temporalCarrierPositiveLimit,
                                        carrierStatus.temporalCarrierNegativeLimit);
                    ImGui::TextDisabled("Headroom-zero pixels: white %.0f, black %.0f",
                                        carrierStatus.temporalCarrierWhiteLimitedPixels,
                                        carrierStatus.temporalCarrierBlackLimitedPixels);
                    ImGui::TextDisabled("Raw Proxy outside carrier range: below 0 %.0f, above 1 %.0f pixels",
                                        carrierStatus.temporalCarrierProxyBelowZeroPixels,
                                        carrierStatus.temporalCarrierProxyAboveOnePixels);
                    if (gainMode == 0 && manualGainSetting.value_or_default() > carrierStatus.temporalCarrierSafeK)
                        ImGui::TextDisabled("Manual K exceeds the current strict safe K; linear/image carrier may clip.");
                }
                else
                {
                    ImGui::TextDisabled("Carrier K telemetry appears after the first completed temporal frame.");
                }
                HelpMarker("Auto Strict takes the most restrictive pixel/channel in the current frame outside any enabled "
                           "NVIDIA-watermark exclusion footprints, so the encoder does not intentionally clip the scene edit. "
                           "With Extended carrier range enabled, the linear/image carrier safety interval is [-1,2] instead "
                           "of [0,1]. Auto Robust ignores one most restrictive source sample inside each approximately 32x32 "
                           "tile before taking the global minimum; it can preserve a much larger K when isolated outliers "
                           "dominate, but those trimmed outliers may clip. K is computed on the GPU before the same frame's "
                           "DLAA pass. Auto K drops immediately when safety requires it; upward changes are rate-limited in "
                           "exposure stops per second so DLAA history does not see abrupt carrier-amplitude jumps.");
            }

            int radius = (int) std::min(config->DlssNrGuidedResidualRadius.value_or_default(), 3u);
            if (ImGui::SliderInt("Guided radius", &radius, 1, 3, "%d P50 px"))
                config->DlssNrGuidedResidualRadius = (uint32_t) radius;
            Slider("Guided range sigma", config->DlssNrGuidedResidualRangeSigma,
                   0.005f, 0.200f, "%.3f", 0.015f);
            Slider("Guided spatial sigma", config->DlssNrGuidedResidualSpatialSigma,
                   0.25f, 3.0f, "%.2f", 1.20f);
            Slider("Guided strength", config->DlssNrGuidedResidualGuideStrength,
                   0.0f, 1.0f, "%.2f", 0.75f);

            static const char* shapingNames[] = { "Off", "Auto by NR style", "Manual" };
            int shaping = (int) std::min(config->DlssNrGuidedResidualShaping.value_or_default(), 2u);
            if (ImGui::Combo("Residual frequency shaping", &shaping, shapingNames, IM_ARRAYSIZE(shapingNames)))
                config->DlssNrGuidedResidualShaping = (uint32_t) shaping;
            if (shaping == 2)
            {
                ImGui::TextUnformatted("Manual P50 / 1-pass base gains");
                Slider("Standard high/mid##guided", config->DlssNrGuidedResidualStandardHighGain,
                       0.0f, 1.5f, "%.3f", 0.421f);
                Slider("Standard low##guided", config->DlssNrGuidedResidualStandardLowGain,
                       0.0f, 1.5f, "%.3f", 0.609f);
                Slider("Natural high/mid##guided", config->DlssNrGuidedResidualHighGain,
                       0.0f, 1.5f, "%.3f", 0.454f);
                Slider("Natural low##guided", config->DlssNrGuidedResidualLowGain,
                       0.0f, 1.5f, "%.3f", 0.741f);
                Slider("Cinematic high/mid##guided", config->DlssNrGuidedResidualCinematicHighGain,
                       0.0f, 1.5f, "%.3f", 0.370f);
                Slider("Cinematic low##guided", config->DlssNrGuidedResidualCinematicLowGain,
                       0.0f, 1.5f, "%.3f", 0.780f);
            }

            if (shaping != 0)
            {
                bool compoundPasses = config->DlssNrGuidedResidualCompoundPasses.value_or_default();
                if (ImGui::Checkbox("Compound high/mid by pass count", &compoundPasses))
                    config->DlssNrGuidedResidualCompoundPasses = compoundPasses;
                HelpMarker("On: after working-scale correction, high/mid is raised to the number of effective NR passes "
                           "(for example 0.85 -> 0.85^2 -> 0.85^3). Off: pass count does not change high/mid. "
                           "Low frequency is never compounded by pass count.");
            }

            const unsigned int runtimePassLimit =
                config->DlssNrUnlockPasses.value_or_default()
                    ? static_cast<unsigned int>(std::size(config->DlssNrPassOverrides) + 1u)
                    : 3u;
            const unsigned int effectivePasses =
                std::clamp(config->DlssNrPasses.value_or_default(), 1u, runtimePassLimit);
            const float shapingScale =
                std::clamp(config->DlssNrWorkingScale.value_or_default(), 0.25f, 2.0f);
            static const char* shapingStyleNames[] = { "Standard", "Natural", "Cinematic" };
            const uint32_t finalShapingStyle =
                Profiles::PassSettings(*config, effectivePasses > 0 ? effectivePasses - 1u : 0u).style;
            if (shaping != 0)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Effective frequency gains now");
                for (uint32_t style = 0; style < 3; ++style)
                {
                    const auto effective =
                        Profiles::EffectiveGuidedResidualGains(*config, (uint32_t) shaping, (uint32_t) transfer,
                                                              style, effectivePasses, shapingScale);
                    ImGui::Text("%s%s: high/mid %.4f, low %.4f", shapingStyleNames[style],
                                style == finalShapingStyle ? " (final style)" : "", effective.high, effective.low);
                }
                if (shaping == 1 && transfer != 7)
                    ImGui::TextDisabled("Auto is neutral for this temporal DLAA mode until it is capture-calibrated.");
                ImGui::TextDisabled("Scale %.0f%%, %u pass%s. High/mid pass compounding: %s; low never compounds.",
                                    shapingScale * 100.0f, effectivePasses, effectivePasses == 1 ? "" : "es",
                                    config->DlssNrGuidedResidualCompoundPasses.value_or_default() ? "ON" : "OFF");
            }

            bool shadowGate = config->DlssNrGuidedResidualShadowGate.value_or_default();
            if (ImGui::Checkbox("Experimental shadow confidence", &shadowGate))
                config->DlssNrGuidedResidualShadowGate = shadowGate;
            if (shadowGate)
            {
                Slider("Shadow confidence floor", config->DlssNrGuidedResidualShadowFloor,
                       0.0f, 1.0f, "%.2f", 0.15f);
                Slider("Shadow threshold low", config->DlssNrGuidedResidualShadowLow,
                       0.0f, 0.25f, "%.3f", 0.02f);
                Slider("Shadow threshold high", config->DlssNrGuidedResidualShadowHigh,
                       0.0f, 0.50f, "%.3f", 0.08f);
            }

            HelpMarker("Capture-derived default: 3x3, range sigma 0.015, spatial sigma 1.20, 75% guided. "
                       "For plain P100-guided residual, Auto shaping is calibrated for one effective pass: 0.421/0.609 "
                       "Standard, 0.454/0.741 Natural, 0.370/0.780 Cinematic (high-mid/low). The two temporal DLAA modes "
                       "leave Auto shaping neutral until they get their own NR50/NR100 capture fit; Manual still works. "
                       "Base gains are P50 / one-pass values. Resolution correction is geometric: "
                       "gain(scale) = gain50 ^ log2(1/scale), so 0.90 gives 1.00 at P100, 0.90 at P50 and 0.81 at P25. "
                       "High/mid can optionally compound once per effective pass; the UI toggle can disable that "
                       "additional pass-count calculation. Low remains pass-independent. The low component is an "
                       "8x area-average of E50, "
                       "equivalent to a simple mip3 box chain at exact P50, so Proxy100 itself is never blurred. "
                       "Shadow confidence is optional/experimental because its thresholds came from one capture.");
        }
        if (reduced && (transfer == 2 || transfer == 4 || transfer == 5 || transfer == 6))
        {
            static const char* directUpscalerNames[] = { "Bilinear", "Bicubic", "Catmull-Rom", "Lanczos2",
                                                         "Lanczos3", "Kaiser2", "Kaiser3", "Area", "MAGIC",
                                                         "FSR1", "DLSS" };

            int detailMode = (int) std::min(config->DlssNrDirectDetailRecovery.value_or_default(), 2u);
            int outputUpscaler = (int) std::min(config->DlssNrDirectOutputUpscaler.value_or_default(), 10u);
            int residualDownscaler =
                (int) std::min(config->DlssNrUpscaledResidualDownscaleFilter.value_or_default(), 11u);
            int residualReferenceUpscaler =
                (int) std::min(config->DlssNrUpscaledResidualReferenceUpscaler.value_or_default(), 10u);
            int residualUpscaler = (int) std::min(config->DlssNrUpscaledResidualUpscaler.value_or_default(), 10u);
            int referenceUpscaler =
                (int) std::min(config->DlssNrDirectDetailReferenceUpscaler.value_or_default(), 10u);

            if (transfer == 5)
            {
                if (ImGui::Combo("NR50 -> NR100 upscaler", &outputUpscaler, directUpscalerNames,
                                 IM_ARRAYSIZE(directUpscalerNames)))
                    config->DlssNrDirectOutputUpscaler = (uint32_t) outputUpscaler;

                HelpMarker("Upscaler used on the complete NR50 answer in Direct NR mode. DLSS preserves the current "
                           "temporal Direct-DLSS path; the other entries are spatial A/B test paths. Area is effectively "
                           "Point/Nearest for exact P50 -> P100 and uses area-weighted blending for non-integer ratios "
                           "such as P60 -> P100.");

                static const char* detailNames[] = { "Off", "Full lost detail", "NR-gated" };
                if (ImGui::Combo("Direct detail recovery", &detailMode, detailNames, IM_ARRAYSIZE(detailNames)))
                    config->DlssNrDirectDetailRecovery = (uint32_t) detailMode;

                HelpMarker("Full lost detail compares P100 against a reconstructed P50 at P100 resolution. NR-gated "
                           "starts from that same restore and suppresses it only where NR50 clearly removed or "
                           "reversed the corresponding P50 local structure.");

                const bool experimentNeedsReference =
                    config->DlssNrExperimentStructureTransfer.value_or_default() != 0;
                if (detailMode != 0 || experimentNeedsReference)
                {
                    if (ImGui::Combo("P50 -> P100 detail reference", &referenceUpscaler, directUpscalerNames,
                                     IM_ARRAYSIZE(directUpscalerNames)))
                        config->DlssNrDirectDetailReferenceUpscaler = (uint32_t) referenceUpscaler;

                    HelpMarker("Only affects detection of detail lost by P100 -> P50. FSR1 is the default reconstruction "
                               "filter. DLSS uses a separate private temporal history from the NR output.");

                    static const char* detailExecutionNames[] = { "Auto", "Serial", "Async compute" };
                    int detailExecution =
                        (int) std::min(config->DlssNrDirectDetailReferenceExecutionMode.value_or_default(), 2u);
                    ImGui::BeginDisabled(referenceUpscaler == 10);
                    if (ImGui::Combo("P50 detail reference execution", &detailExecution, detailExecutionNames,
                                     IM_ARRAYSIZE(detailExecutionNames)))
                        config->DlssNrDirectDetailReferenceExecutionMode = (uint32_t) detailExecution;
                    ImGui::EndDisabled();
                    HelpMarker("Serial keeps the previous path unchanged. Auto overlaps a non-DLSS P50 -> "
                               "P100 reference only when OptiScaler owns/controls the DX12 command submission "
                               "(including Finished Picture and independent bridge paths). Explicit Async compute also "
                               "allows the native caller-owned DX12 list to be split early; that native override is "
                               "experimental because a game may enqueue queue-level waits only after recording. "
                               "Unsupported cases still fall back to Serial. A DLSS detail reference is always Serial.");

                    if (detailMode == 2)
                        Slider("NR gate strength", config->DlssNrDirectDetailMaskStrength, 0.0f, 100.0f, "%.0f%%",
                               100.0f);
                }
            }

            if (transfer == 6)
            {
                static const char* residualDownNames[] = { "Area", "Bilinear", "Catmull-Rom", "Lanczos2",
                                                           "Point / nearest", "FSR1", "Bicubic", "Lanczos3",
                                                           "Kaiser2", "Kaiser3", "MAGIC",
                                                           "SSIM Sharp (experimental)" };

                if (ImGui::Combo("P100 -> P50 downscaler", &residualDownscaler, residualDownNames,
                                 IM_ARRAYSIZE(residualDownNames)))
                    config->DlssNrUpscaledResidualDownscaleFilter = (uint32_t) residualDownscaler;

                if (ImGui::Combo("P50 -> P100 upscaler", &residualReferenceUpscaler, directUpscalerNames,
                                 IM_ARRAYSIZE(directUpscalerNames)))
                    config->DlssNrUpscaledResidualReferenceUpscaler = (uint32_t) residualReferenceUpscaler;

                static const char* residualExecutionNames[] = { "Auto", "Serial", "Async compute" };
                int residualExecution =
                    (int) std::min(config->DlssNrUpscaledResidualReferenceExecutionMode.value_or_default(), 2u);
                ImGui::BeginDisabled(residualReferenceUpscaler == 10);
                if (ImGui::Combo("P50 -> P100 execution", &residualExecution, residualExecutionNames,
                                 IM_ARRAYSIZE(residualExecutionNames)))
                    config->DlssNrUpscaledResidualReferenceExecutionMode = (uint32_t) residualExecution;
                ImGui::EndDisabled();

                if (ImGui::Combo("NR50 -> NR100 upscaler", &residualUpscaler, directUpscalerNames,
                                 IM_ARRAYSIZE(directUpscalerNames)))
                    config->DlssNrUpscaledResidualUpscaler = (uint32_t) residualUpscaler;

                HelpMarker("All three resize legs are independent. P50 -> P100 can run on the separate async compute "
                           "queue for non-DLSS scalers, in parallel with NR50 processing, and joins only before the "
                           "native-resolution residual is created. DLSS reference scaling stays Serial and owns a "
                           "separate temporal history from NR50 -> NR100.");
            }

            const bool directExperimentNeedsReference =
                config->DlssNrExperimentStructureTransfer.value_or_default() != 0;
            const bool directUsesDlss =
                transfer == 5 &&
                (outputUpscaler == 10 || ((detailMode != 0 || directExperimentNeedsReference) && referenceUpscaler == 10));
            const bool upscaledResidualUsesDlss =
                transfer == 6 && (residualReferenceUpscaler == 10 || residualUpscaler == 10);
            if (transfer == 2 || transfer == 4 || directUsesDlss || upscaledResidualUsesDlss)
            {
                static const char* presetNames[] = { "Default", "A", "B", "C", "D", "E", "F", "G", "H",
                                                     "I", "J", "K", "L", "M", "N", "O", "Latest" };
                static constexpr int presetValues[] = {
                    NVSDK_NGX_DLSS_Hint_Render_Preset_Default,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_A,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_B,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_C,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_D,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_E,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_F,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_G,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_J,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_K,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_L,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_M,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_N,
                    NVSDK_NGX_DLSS_Hint_Render_Preset_O,
                    static_cast<int>(NV_PRESET_LATEST)
                };
                int presetIndex = 1;
                const int configuredPreset = config->DlssNrScalingDlssPreset.value_or_default();
                for (int i = 0; i < IM_ARRAYSIZE(presetValues); ++i)
                    if (presetValues[i] == configuredPreset)
                        presetIndex = i;

                if (ImGui::Combo("DLSS preset", &presetIndex, presetNames, IM_ARRAYSIZE(presetNames)))
                    config->DlssNrScalingDlssPreset = presetValues[presetIndex];

                HelpMarker("NGX render preset for any private DLSS SR feature used by NR enlargement or P50 "
                           "detail-reference reconstruction.");

                const bool mainDlssActive =
                    transfer == 2 || transfer == 4 ||
                    (transfer == 5 && outputUpscaler == 10) ||
                    (transfer == 6 && residualUpscaler == 10);
                const bool referenceDlssActive =
                    (transfer == 5 && (detailMode != 0 || directExperimentNeedsReference) && referenceUpscaler == 10) ||
                    (transfer == 6 && residualReferenceUpscaler == 10);

                if (mainDlssActive)
                {
                    bool autoExposure = config->DlssNrScalingDlssAutoExposure.value_or_default();
                    if (ImGui::Checkbox("Main DLSS Auto Exposure (experimental)", &autoExposure))
                        config->DlssNrScalingDlssAutoExposure = autoExposure;
                    HelpMarker("Sets NVSDK_NGX_DLSS_Feature_Flags_AutoExposure on the main private DLSS SR feature "
                               "used for the selected enlargement/output path.");
                }

                if (referenceDlssActive)
                {
                    bool autoExposure = config->DlssNrDetailReferenceDlssAutoExposure.value_or_default();
                    if (ImGui::Checkbox("P50 reference DLSS Auto Exposure (experimental)", &autoExposure))
                        config->DlssNrDetailReferenceDlssAutoExposure = autoExposure;
                    HelpMarker("Sets NVSDK_NGX_DLSS_Feature_Flags_AutoExposure only on the separate P50 -> P100 "
                               "DLSS reference history.");
                }
            }

            if ((transfer == 5 || transfer == 6) && ImGui::TreeNode("Detail quality lab (experimental)"))
            {
                ImGui::TextDisabled("Slider ranges are only for dragging. Ctrl+click a slider to type any finite value; typed values are not clamped.");

                ImGui::TextUnformatted("Input preparation");
                static const char* inputFilterNames[] = {
                    "Off", "Uniform softening", "Edge-selective isotropic", "Edge-selective normal",
                    "Edge-core attenuation"
                };
                int inputFilter =
                    (int) std::min(config->DlssNrExperimentInputFilter.value_or_default(), 4u);
                if (ImGui::Combo("P50 input preparation", &inputFilter, inputFilterNames,
                                 IM_ARRAYSIZE(inputFilterNames)))
                    config->DlssNrExperimentInputFilter = (uint32_t) inputFilter;
                HelpMarker("Runs after the selected P100 -> reduced proxy filter and before NVIDIA NR. "
                           "The legacy soften modes and Edge-core attenuation each use one low-resolution dispatch. "
                           "Edge-core attenuation reduces only a detected thin edge core instead of spreading it into the background.");

                if (inputFilter >= 1 && inputFilter <= 3)
                {
                    Slider("Input soften radius", config->DlssNrExperimentInputRadius, 0.25f, 2.0f,
                           "%.2f px", 0.75f);
                    Slider("Input soften strength", config->DlssNrExperimentInputStrength, 0.0f, 1.0f,
                           "%.2f", 1.0f);
                    if (inputFilter >= 2)
                        Slider("Edge threshold", config->DlssNrExperimentEdgeThreshold, 0.005f, 0.20f,
                               "%.3f", 0.04f);
                }
                else if (inputFilter == 4)
                {
                    static const char* coreNames[] = { "Off", "Luma-only", "RGB" };
                    int coreMode =
                        (int) std::min(config->DlssNrExperimentCoreAttenuation.value_or_default(), 2u);
                    if (ImGui::Combo("Edge core attenuation", &coreMode, coreNames, IM_ARRAYSIZE(coreNames)))
                        config->DlssNrExperimentCoreAttenuation = (uint32_t) coreMode;
                    if (coreMode != 0)
                    {
                        Slider("Core attenuation strength", config->DlssNrExperimentCoreAttenuationStrength,
                               0.0f, 1.0f, "%.2f", 0.75f);
                        Slider("Core detection threshold", config->DlssNrExperimentCoreDetectionThreshold,
                               0.001f, 0.20f, "%.3f", 0.04f);
                        Slider("Core width", config->DlssNrExperimentCoreWidth,
                               0.25f, 4.0f, "%.2f px", 1.0f);
                        Slider("Core halo protection", config->DlssNrExperimentCoreHaloProtection,
                               0.0f, 1.0f, "%.2f", 1.0f);
                    }
                    HelpMarker("Luma-only moves the detected core only along the neutral/luma axis; RGB moves it "
                               "toward the two samples across the thin feature. Halo protection rejects asymmetric "
                               "step edges so the operation does not create a broad translucent band.");
                }

                if (inputFilter != 0)
                {
                    static const char* referenceNames[] = {
                        "Soft/model input", "Sharp original P50"
                    };
                    int reference =
                        (int) std::min(config->DlssNrExperimentReferenceSource.value_or_default(), 1u);
                    if (ImGui::Combo("P50 detail reference source", &reference, referenceNames,
                                     IM_ARRAYSIZE(referenceNames)))
                        config->DlssNrExperimentReferenceSource = (uint32_t) reference;
                    HelpMarker("Soft/model input reconstructs exactly what NVIDIA NR saw; Sharp original P50 "
                               "reconstructs S. With Cancel input-preparation footprint enabled, all later P50-derived "
                               "references are forced to sharp S in both Direct NR and Upscaled NR residual so B-S "
                               "cannot be reintroduced and structure gain is measured against the rebased baseline.");

                    static const char* cancelNames[] = { "Off", "On" };
                    int cancelFootprint =
                        config->DlssNrExperimentCancelInputPreparationFootprint.value_or_default() ? 1 : 0;
                    if (ImGui::Combo("Cancel input-preparation footprint", &cancelFootprint,
                                     cancelNames, IM_ARRAYSIZE(cancelNames)))
                        config->DlssNrExperimentCancelInputPreparationFootprint = cancelFootprint != 0;
                    HelpMarker("When enabled, NR's response is rebased from the prepared P50 back onto the original "
                               "sharp P50: S + (N - B). This preserves the model response while removing the "
                               "preparation footprint itself.");
                }

                ImGui::Spacing();
                ImGui::TextUnformatted("NR50 artifact control");
                static const char* nrFilterNames[] = {
                    "Off", "Uniform NR50 softening", "NR-edge isotropic", "NR-edge normal",
                    "NR excess-only normal"
                };
                int nrFilter =
                    (int) std::min(config->DlssNrExperimentNrEdgeFilter.value_or_default(), 4u);
                if (ImGui::Combo("NR50 edge treatment", &nrFilter, nrFilterNames, IM_ARRAYSIZE(nrFilterNames)))
                    config->DlssNrExperimentNrEdgeFilter = (uint32_t) nrFilter;
                HelpMarker("Optional treatment inside the same low-resolution post-NR pass. Excess-only compares "
                           "NR50 against the exact model input and softens only edge energy NR added beyond that input.");
                if (nrFilter != 0)
                {
                    Slider("NR edge radius", config->DlssNrExperimentNrEdgeRadius, 0.25f, 2.0f,
                           "%.2f px", 0.75f);
                    Slider("NR edge soften strength", config->DlssNrExperimentNrEdgeStrength, 0.0f, 1.0f,
                           "%.2f", 1.0f);
                }

                static const char* ghostGuardNames[] = {
                    "Off", "Basic", "Edge-only", "Edge-only directional"
                };
                int ghostGuard =
                    (int) std::min(config->DlssNrExperimentGhostGuard.value_or_default(), 3u);
                if (ImGui::Combo("Ghost guard", &ghostGuard, ghostGuardNames, IM_ARRAYSIZE(ghostGuardNames)))
                    config->DlssNrExperimentGhostGuard = (uint32_t) ghostGuard;

                static const char* ghostBandNames[] = { "Off", "One-band", "Two-band" };
                int ghostBands =
                    (int) std::min(config->DlssNrExperimentGhostBandSuppression.value_or_default(), 2u);
                if (ImGui::Combo("Ghost band suppression", &ghostBands, ghostBandNames,
                                 IM_ARRAYSIZE(ghostBandNames)))
                    config->DlssNrExperimentGhostBandSuppression = (uint32_t) ghostBands;

                const bool anyGhostControl = ghostGuard != 0 || ghostBands != 0;
                if (anyGhostControl)
                {
                    Slider("Ghost detection threshold", config->DlssNrExperimentGhostDetectionThreshold,
                           0.001f, 0.20f, "%.3f", 0.01f);
                    if (ghostGuard >= 2 || ghostBands != 0)
                    {
                        Slider("Ghost edge threshold", config->DlssNrExperimentGhostEdgeThreshold,
                               0.001f, 0.20f, "%.3f", 0.04f);
                        Slider("Ghost band radius", config->DlssNrExperimentGhostBandRadius,
                               0.25f, 10.0f, "%.2f px", 2.0f);
                    }
                }
                if (ghostGuard != 0)
                {
                    Slider("Ghost guard strength", config->DlssNrExperimentGhostGuardStrength,
                           0.0f, 1.0f, "%.2f", 1.0f);
                    Slider("Ghost max suppression", config->DlssNrExperimentGhostMaxSuppression,
                           0.0f, 1.0f, "%.2f", 1.0f);
                }
                HelpMarker("Compares S = sharp P50, B = the prepared P50 shown to NR, and N = NR50. "
                           "When B-S and N-B move in the same luminance direction the matching NR edit can be "
                           "suppressed. Edge-only gates that test to sharp P50 edges; directional suppresses only "
                           "RGB components that move in the same direction as the preparation delta.");

                if (ghostBands != 0)
                {
                    Slider("Band suppression strength", config->DlssNrExperimentBandSuppressionStrength,
                           0.0f, 1.0f, "%.2f", 1.0f);
                    Slider("Low band radius", config->DlssNrExperimentLowBandRadius,
                           0.50f, 10.0f, "%.2f px", 3.0f);
                    if (ghostBands >= 2)
                        Slider("Mid band radius", config->DlssNrExperimentMidBandRadius,
                               0.25f, 5.0f, "%.2f px", 1.5f);
                }
                HelpMarker("Suppresses only low/mid-frequency parts of the NR edit where the blur-direction detector "
                           "identifies a ghost. High-frequency structure is left in place. One-band uses the low band; "
                           "Two-band evaluates low and mid bands separately in the same post-NR dispatch.");

                bool scaleAware = config->DlssNrExperimentScaleAwareStructure.value_or_default();
                if (ImGui::Checkbox("Scale-aware Local structure (Standard/Natural)", &scaleAware))
                    config->DlssNrExperimentScaleAwareStructure = scaleAware;
                if (scaleAware)
                    Slider("Local structure factor at P50", config->DlssNrExperimentStructureP50Factor,
                           0.25f, 1.0f, "%.2fx", 0.60f);
                HelpMarker("No shader pass. Multiplies NVIDIA LocalStructureStrength only for Standard/Natural, "
                           "interpolating from 1.0 at P100 to this factor at P50.");

                static const char* limiterNames[] = { "Off", "Fine band", "Fine + mid bands" };
                int limiter =
                    (int) std::min(config->DlssNrExperimentP100EdgeLimiter.value_or_default(), 2u);
                if (ImGui::Combo("P100 edge limiter", &limiter, limiterNames, IM_ARRAYSIZE(limiterNames)))
                    config->DlssNrExperimentP100EdgeLimiter = (uint32_t) limiter;
                if (limiter != 0)
                {
                    Slider("P100 edge limiter strength", config->DlssNrExperimentP100EdgeLimiterStrength,
                           0.0f, 1.0f, "%.2f", 1.0f);
                    Slider("Max edge gain", config->DlssNrExperimentP100EdgeLimiterMaxGain,
                           1.0f, 4.0f, "%.2fx", 1.0f);
                    bool limiterDebug = config->DlssNrExperimentP100EdgeLimiterDebug.value_or_default();
                    if (ImGui::Checkbox("P100 edge limiter debug mask", &limiterDebug))
                        config->DlssNrExperimentP100EdgeLimiterDebug = limiterDebug;
                }
                HelpMarker("Caps the FINAL edge-band energy against untouched P100, rather than comparing the "
                           "NR edit alone. At Max edge gain 1.00x, NR may reduce an existing edge but may not make that "
                           "band stronger than native P100. Fine + mid also limits the wider 1-2 px band. If Structure "
                           "Transfer is enabled too, it runs first and this limiter remains the final cap. Debug mask "
                           "shows where the limiter actually acts.");

                static const char* transferNames[] = {
                    "Off", "Structure gain - one band", "Structure gain - two band"
                };
                int structureTransfer =
                    (int) std::min(config->DlssNrExperimentStructureTransfer.value_or_default(), 2u);
                if (ImGui::Combo("P100 structure transfer", &structureTransfer, transferNames,
                                 IM_ARRAYSIZE(transferNames)))
                    config->DlssNrExperimentStructureTransfer = (uint32_t) structureTransfer;
                if (structureTransfer != 0)
                {
                    Slider("Structure transfer strength", config->DlssNrExperimentStructureTransferStrength,
                           0.0f, 1.0f, "%.2f", 1.0f);

                    bool polarityGuard = config->DlssNrExperimentStructurePolarityGuard.value_or_default();
                    if (ImGui::Checkbox("Structure polarity guard", &polarityGuard))
                        config->DlssNrExperimentStructurePolarityGuard = polarityGuard;
                    HelpMarker("Rejects structure transfer when the P50/NR structure sign disagrees with the native "
                               "P100 band. This targets opposite-side lobes around thin lines that otherwise become "
                               "bright/dark ringing.");

                    static const char* envelopeNames[] = { "Off", "Local envelope", "Soft envelope" };
                    int envelope =
                        (int) std::min(config->DlssNrExperimentStructureEnvelope.value_or_default(), 2u);
                    if (ImGui::Combo("Structure anti-ringing", &envelope, envelopeNames, IM_ARRAYSIZE(envelopeNames)))
                        config->DlssNrExperimentStructureEnvelope = (uint32_t) envelope;
                    if (envelope != 0)
                        Slider("Envelope margin", config->DlssNrExperimentStructureEnvelopeMargin,
                               0.0f, 100.0f, "%.1f%%", 0.0f);
                    HelpMarker("Constrains only the EXTRA change introduced by Structure Transfer against the local "
                               "native-P100 neighbourhood. Ordinary NR edits are left untouched. Local envelope is a "
                               "hard bound; Soft envelope compresses overshoot with a soft knee. Margin adds headroom "
                               "as a percentage of the local P100 RGB range.");

                    Slider("Structure max gain", config->DlssNrExperimentStructureMaxGain,
                           1.0f, 8.0f, "%.2fx", 4.0f);
                    Slider("Structure confidence threshold", config->DlssNrExperimentStructureConfidenceThreshold,
                           0.001f, 0.100f, "%.3f", 0.020f);
                    HelpMarker("Max gain limits the model/reference band ratio. Confidence threshold controls how "
                               "strong the reference band must be before Structure Transfer reaches full weight; below "
                               "it, the result now blends back toward ordinary NR instead of erasing the band.");

                    bool shadowProtection = config->DlssNrExperimentStructureShadowProtection.value_or_default();
                    if (ImGui::Checkbox("Structure shadow protection", &shadowProtection))
                        config->DlssNrExperimentStructureShadowProtection = shadowProtection;
                    if (shadowProtection)
                    {
                        Slider("Shadow threshold", config->DlssNrExperimentStructureShadowThreshold,
                               0.001f, 0.250f, "%.3f", 0.080f);
                        Slider("Shadow protection strength", config->DlssNrExperimentStructureShadowStrength,
                               0.0f, 1.0f, "%.2f", 1.0f);
                    }
                    HelpMarker("Progressively reduces Structure Transfer in very dark native-P100 regions. This is "
                               "intended to suppress isolated bright sparkles inside shadows without globally disabling "
                               "real P100 detail there.");
                }
                HelpMarker("Runs inside final resolve after NR50 artifact control. Low-frequency NR edits are retained, "
                           "while high-frequency geometry comes from untouched P100 and NR supplies the local structure "
                           "gain. Two-band also transfers a second, wider detail band.");

                ImGui::TextDisabled("All controls are independent; Off or a zero strength is neutral.");
                ImGui::TreePop();
            }
        }
    }
    static const char* reversibleNames[] = { "Off (soft knee)", "Neutwo proxy + composed", "Neutwo proxy + replace",
                                             "Hybrid proxy + composed", "Hybrid proxy + replace" };
    int reversible = (int) config->DlssNrReversibleMode.value_or_default();
    if (reversible < 0 || reversible > 4)
        reversible = 0;
    if (ImGui::Combo("HDR mapping (experimental)", &reversible, reversibleNames, IM_ARRAYSIZE(reversibleNames)))
        config->DlssNrReversibleMode = (uint32_t) reversible;

    HelpMarker("HDR mapping curve. Replace bypasses strength and highlight controls.");

    if (reversible == 2 || reversible == 4)
        Slider("Restore sharpness", config->DlssNrReplaceDetailStrength, 0.0f, 2.0f, "%.2f", 0.0f);

    const char* exposureNames[] = { "Manual", "Game exposure", "Automatic HDR exposure" };
    const auto source = config->DlssNrWhitePointSource.value_or_default();
    int selected = source == 3 ? 2 : source == 1 ? 1 : 0;
    if (ImGui::Combo("White point source", &selected, exposureNames, 3))
        config->DlssNrWhitePointSource = selected == 2 ? 3u : static_cast<unsigned>(selected);
    if (selected)
    {
        auto& trim = selected == 2 ? config->DlssNrAutoExposureTrim : config->DlssNrWhitePointTrim;
        Slider("Exposure trim", trim, 0.001f, 1000.0f, "%.3fx", selected == 2 ? 5.0f : 1.0f,
               ImGuiSliderFlags_Logarithmic);
        if (selected == 2)
            Slider("Highlight protection", config->DlssNrAutoExposureHighlightProtection, 0.0f, 100.0f, "%.0f%%", 0.0f);
        auto& curve = selected == 2 ? config->DlssNrAutoExposureTrimAnchors : config->DlssNrExposureTrimAnchors;
        char text[512] {};
        const auto value = curve.value_or_default();
        std::memcpy(text, value.data(), std::min(value.size(), sizeof(text) - 1));
        if (ImGui::InputText("Trim anchors", text, sizeof(text)))
            curve = std::string(text);
        HelpMarker("Up to eight base-white-point:trim pairs, e.g. 1:5 100:2. Trim interpolates logarithmically. Clear "
                   "for a fixed trim.");
    }
    Slider("Paper white", config->DlssNrWhitePointScale, 0.25f, 2000.0f, "%.2fx", {}, ImGuiSliderFlags_Logarithmic);
    HelpMarker("Higher values darken the NR input; lower values brighten it.");
}

// Model tuning rebuilds the feature; commit slider changes only on release.
template <typename Option>
static void DeferredSlider(const char* label, Option* opt, float mn, float mx, float def, bool inheritReset = false)
{
    static std::unordered_map<ImGuiID, float> pending;
    const ImGuiID id = ImGui::GetID(label);

    auto it = pending.find(id);
    float value = it != pending.end() ? it->second : (opt->value_or(def));

    if (ImGui::SliderFloat(label, &value, mn, mx, "%.2f"))
        pending[id] = value;

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        *opt = std::clamp(value, mn, mx);
        pending.erase(id);
    }

    ImGui::SameLine();

    const std::string resetId = std::string("Reset##") + label;
    if (ImGui::SmallButton(resetId.c_str()))
    {
        if (inheritReset)
            *opt = std::optional<float> {};
        else
            *opt = def;
        pending.erase(id);
    }

    if (std::strcmp(label, "Intensity") == 0)
        HelpMarker("Enhancement strength. 1 = default.");
    else if (std::strcmp(label, "Local structure") == 0)
        HelpMarker("Fine detail and local contrast. 1 = default.");
    else if (std::strcmp(label, "Local tone") == 0)
        HelpMarker("Broad lighting changes. Later passes default to 0.");
    else if (std::strcmp(label, "Skin structure") == 0)
        HelpMarker("Skin detail. -1 follows Local structure.");
}

void RenderModel(Config* config)
{
    bool unlockPasses = config->DlssNrUnlockPasses.value_or_default();
    const int menuPassLimit = unlockPasses ? 10 : 2;
    static int passes = 1;
    static bool editingPasses = false;
    if (!editingPasses)
        passes = (int) std::clamp(config->DlssNrPasses.value_or_default(), 1u, (unsigned) menuPassLimit);
    ImGui::SliderInt("Model passes", &passes, 1, menuPassLimit, "%d", ImGuiSliderFlags_AlwaysClamp);
    editingPasses = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit())
        config->DlssNrPasses = (uint32_t) std::clamp(passes, 1, menuPassLimit);
    if (ImGui::Checkbox("Unlock up to 10 passes", &unlockPasses))
    {
        config->DlssNrUnlockPasses = unlockPasses;
        config->DlssNrPasses = std::clamp(config->DlssNrPasses.value_or_default(), 1u, unlockPasses ? 10u : 2u);
    }

    const bool interPassAvailable =
        config->DlssNrPasses.value_or_default() > 1u &&
        config->DlssNrWorkingScale.value_or_default() < 0.999f &&
        !config->DlssNrSpatialCompression.value_or_default();
    if (interPassAvailable)
    {
        static const char* interPassNames[] = {
            "Off", "P100-guided -> P100 -> working-res", "P100-guided fused -> working-res"
        };
        int interPass = (int) std::min(config->DlssNrInterPassReconstruction.value_or_default(), 2u);
        if (ImGui::Combo("Inter-pass reconstruction", &interPass, interPassNames, IM_ARRAYSIZE(interPassNames)))
            config->DlssNrInterPassReconstruction = (uint32_t) interPass;
        HelpMarker("Reconstructs the cumulative NR edit against untouched native P100 between model passes, then "
                   "feeds the corrected working-resolution image to the next NR pass. Final transfer remains unchanged.");
        HelpMarker("Inter-pass frequency shaping uses the currently selected guided residual gains once per transition. "
                   "Compound-pass gain scaling is intentionally ignored.");
    }

    static unsigned selectedPass = 0;
    const auto selectedLabel = std::format("Pass {}", selectedPass + 1);
    if (ImGui::BeginCombo("Edit pass", selectedLabel.c_str()))
    {
        for (unsigned pass = 0; pass <= std::size(config->DlssNrPassOverrides); ++pass)
        {
            const auto label = std::format("Pass {}", pass + 1);
            if (ImGui::Selectable(label.c_str(), selectedPass == pass))
                selectedPass = pass;
            if (selectedPass == pass)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (selectedPass >= config->DlssNrPasses.value_or_default())
        ImGui::TextDisabled("Inactive pass. Settings apply when this pass is enabled.");

    // Distinct widget IDs keep uncommitted slider edits with their selected pass.
    ImGui::PushID((int) selectedPass);
    const bool inherited = selectedPass != 0;
    const auto tuning = [&](auto& intensity, auto& structure, auto& tone, auto& skin, auto& autoMask)
    {
        DeferredSlider("Intensity", &intensity, 0.0f, 2.0f,
                       inherited ? config->DlssNrIntensity.value_or_default() : 1.0f, inherited);
        DeferredSlider("Local structure", &structure, 0.0f, 2.0f,
                       inherited ? config->DlssNrLocalStructure.value_or_default() : 1.0f, inherited);
        DeferredSlider("Local tone", &tone, 0.0f, 2.0f, inherited ? 0.0f : 1.0f, inherited);
        DeferredSlider("Skin structure", &skin, -1.0f, 2.0f,
                       inherited ? config->DlssNrSkinStructure.value_or_default() : -1.0f, inherited);
        bool mask = autoMask.value_or(inherited ? config->DlssNrAutoMask.value_or_default() : true);
        if (ImGui::Checkbox("Auto skin mask", &mask))
            autoMask = mask;
        if (inherited)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##mask"))
                autoMask = std::optional<bool> {};
        }
        HelpMarker("Model-based skin selection.");
    };
    static const char* styles[] = { "Standard", "Natural", "Cinematic" };
    static const char* inheritedStyles[] = { "Auto", "Standard", "Natural", "Cinematic" };
    if (selectedPass == 0)
    {
        int style = (int) std::min(config->DlssNrStyle.value_or_default(), 2u);
        if (ImGui::Combo("Style", &style, styles, IM_ARRAYSIZE(styles)))
            config->DlssNrStyle = (uint32_t) style;
        tuning(config->DlssNrIntensity, config->DlssNrLocalStructure, config->DlssNrLocalTone,
               config->DlssNrSkinStructure, config->DlssNrAutoMask);
    }
    else
    {
        auto& pass = config->DlssNrPassOverrides[selectedPass - 1];
        int style = pass.style.has_value() ? std::clamp((int) pass.style.value(), 0, 2) + 1 : 0;
        if (ImGui::Combo("Style", &style, inheritedStyles, IM_ARRAYSIZE(inheritedStyles)))
            pass.style = style ? std::optional<uint32_t>(style - 1) : std::nullopt;
        tuning(pass.intensity, pass.structure, pass.tone, pass.skin, pass.autoMask);
    }
    ImGui::PopID();
}

void RenderBlend(Config* config)
{
    if (config->DlssNrResidualAcrossRr.value_or_default())
        Slider("History confidence threshold", config->DlssNrResidualConfidenceSensitivity, 0.0f, 2.0f, "%.3f", 0.0f);
    if (config->DlssNrFinishedPicture.value_or_default() &&
        (config->DlssNrRunBeforeSr.value_or_default() || config->DlssNrDeferredDlss.value_or_default()))
    {
        const auto feature = State::Instance().currentFeature;
        ImGui::BeginDisabled(State::Instance().swapchainApi == API::Vulkan ||
                             (feature && feature->GetUpscalerType() == Upscaler::DLSSD));
        Checkbox("Match HDR brightness response (experimental)", config->DlssNrHdrTransfer);
        ImGui::EndDisabled();
        HelpMarker(
            "Match early NR brightness changes to the finished HDR image. Adds GPU work; unreliable fits fall back.");
    }
    Slider("Detail strength", config->DlssNrTransferStrength, 0.0f, 2.0f, "%.2f", 1.0f);
    HelpMarker("0 = no detail change. 1 = normal.");

    Slider("Colour strength", config->DlssNrColourStrength, 0.0f, 4.0f, "%.2f", 1.0f);
    HelpMarker("0 = game colours. 1 = model colours. Above 1 boosts saturation.");

    if (ImGui::TreeNode("Skin and environment (final edit)"))
    {
        Checkbox("Separate skin / environment controls", config->DlssNrSkinProtection);
        ImGui::BeginDisabled(!config->DlssNrSkinProtection.value_or_default());
        const auto slider = [](const char* label, auto& option)
        {
            Slider(label, option, 0.0f, 1.0f);
            HelpMarker("0 = unchanged. 1 = full effect.");
        };
        slider("Skin detail / lighting", config->DlssNrSkinDetail);
        slider("Skin colour", config->DlssNrSkinColour);
        slider("Environment detail / lighting", config->DlssNrEnvironmentDetail);
        slider("Environment colour", config->DlssNrEnvironmentColour);
        Checkbox("Preview colour-based mask", config->DlssNrShowSkinMask);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    Slider("Highlight guard", config->DlssNrMaxRatio, 1.0f, 8.0f, "%.1fx", 2.0f);
    HelpMarker("Maximum brightening multiplier. Only excessive brightening is limited.");

    Slider("Darkening guard", config->DlssNrMaxDarkening, 0.0f, 100.0f, "%.0f%%", 100.0f);
    HelpMarker("Maximum luminance reduction. 100% leaves darkening uncapped; 50% prevents pixels from becoming darker than half their original luminance; 0% prevents darkening.");
}

void RenderInspect(Config* config)
{
    const auto placement = DlssNr::ResolvePlacement(
        config->DlssNrRunBeforeSr.value_or_default(), config->DlssNrDeferredDlss.value_or_default(),
        config->DlssNrResidualAcrossRr.value_or_default(), config->DlssNrFinishedPicture.value_or_default());
    if (placement.deferred && (config->DlssNrCompare.value_or_default() || config->DlssNrDebugView.value_or_default() ||
                               config->DlssNrShowSkinMask.value_or_default()))
        ImGui::TextWrapped("Compare, debug view and skin-mask inspection suspend the separate edit-upscale path.");
    Checkbox("Hold frame", config->DlssNrHoldFrame);
    HelpMarker(
        "Freeze a frame for NR tuning. Later game effects may update; temporal behaviour is not representative.");

    static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
    int compare = (int) config->DlssNrCompare.value_or_default();
    if (ImGui::Combo("Compare", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
        config->DlssNrCompare = (uint32_t) compare;

    HelpMarker("Compare the original and NR output.");

    if (compare != 0)
    {
        Checkbox("Swap sides", config->DlssNrCompareSwap);
        Checkbox("Label the sides", config->DlssNrCompareTags);
        if (config->DlssNrCompareTags.value_or_default())
            Slider("Label size", config->DlssNrTagScale, 0.5f, 5.0f, "%.1fx", {}, ImGuiSliderFlags_AlwaysClamp);
    }

    if (compare == 1)
    {
        Slider("Zoom", config->DlssNrCompareZoom, 1.0f, 2.0f, "%.2f", {}, ImGuiSliderFlags_AlwaysClamp);
        HelpMarker("1 = fit. 2 = crop and enlarge.");
    }

    if (compare == 2)
    {
        Slider("Split", config->DlssNrCompareSplit, 0.0f, 1.0f, "%.2f", {}, ImGuiSliderFlags_AlwaysClamp);
        HelpMarker("Move the comparison boundary.");
    }

    static const char* debugNames[] = { "Off", "Proxy (what the model sees)", "Model output (raw)",
                                        "Difference (amplified)", "Compressed model input",
                                        "Temporal carrier before DLAA",
                                        "Decoded temporal residual (20x, zero=grey)",
                                        "Inter-pass corrected input (pass 2)" };
    int debugView = (int) config->DlssNrDebugView.value_or_default();
    if (ImGui::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
        config->DlssNrDebugView = (uint32_t) debugView;

    HelpMarker("Difference is amplified 20x. Grey means unchanged. Proxy and raw model output use unpacked geometry. "
               "Compressed model input shows the input before unpacking, scaled to fill the screen. Temporal carrier "
               "before DLAA is available in Temporal residual/image-anchored modes. Decoded temporal residual maps signed "
               "zero to 50% grey and amplifies it 20x, avoiding the misleading mostly-black raw signed view. Inter-pass "
               "corrected input snapshots the exact bounded working-resolution image fed to pass 2.");
}
} // namespace DlssNr::MenuSections
