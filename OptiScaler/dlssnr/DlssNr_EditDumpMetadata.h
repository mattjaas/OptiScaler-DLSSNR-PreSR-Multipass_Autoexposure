#pragma once
#include "DlssNr_GainPreview.h"
#include "DlssNr_DepthTransport.h"
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>

namespace DlssNr::EditDump
{
struct PreviewSnapshot
{
    float suggestedGain = 0, rawGain = 0, movingPercent = 0, changedPercent = 0;
    double ageSeconds = -1;
    uint64_t measurements = 0;
    uint32_t method = GainPreview::Coverage, boundaryPairs = 0;
    float boundaryBase = 0, boundarySlope = 0, boundaryTarget = GainPreview::BoundaryTarget;
    bool valid = false, paused = true, catchingUp = false;
};
inline PreviewSnapshot SnapshotPreview(const GainPreview::Status& status, double now)
{
    PreviewSnapshot s;
    s.valid = status.valid;
    s.paused = status.paused;
    s.catchingUp = status.catchingUp;
    s.suggestedGain = status.smoothed;
    s.rawGain = status.raw;
    s.movingPercent = status.motionPercent;
    s.changedPercent = status.changedPercent;
    s.measurements = status.measurements;
    s.method = status.method;
    s.boundaryPairs = status.boundaryPairs;
    s.boundaryBase = status.boundaryBase;
    s.boundarySlope = status.boundarySlope;
    s.boundaryTarget = status.boundaryTarget;
    s.ageSeconds = status.valid && std::isfinite(now) && std::isfinite(status.measuredAt)
                       ? std::max(0., now - status.measuredAt)
                       : -1;
    return s;
}
struct Metadata
{
    DepthTransport::Settings depthTransport;
    bool depthIncluded = false, originalGuides = false;
    unsigned dlssDepthW = 0, dlssDepthH = 0, dlssMotionW = 0, dlssMotionH = 0;
    unsigned dlssDepthX = 0, dlssDepthY = 0, dlssMotionX = 0, dlssMotionY = 0;
    float dlssMotionScaleX = 1, dlssMotionScaleY = 1;
    uint64_t frame = 0, submissionEpoch = 0;
    uint32_t width = 0, height = 0, privatePreset = 0, nrPresetSetting = 0, exposureMode = 0;
    uint32_t whitePointSource = 0, expectedPassMask = 0, completedPassMask = 0;
    float gain = 1, saturation = 1, depthStrength = 0, manualExposure = 1;
    float autoTrimControl = 1, gameTrimControl = 1, scanTrimControl = 1;
    bool fullNrAnswer = false, invertedDepth = false, privateIsHdr = true;
    bool packed = false, reset = false, held = false, guidesIncluded = false;
    bool previewEnabledAtRequest = false, previewEnabledAtCapture = false;
    uint32_t privatePresetAtRequest = 0;
    uint32_t cnnPresetOverride = 0, transformerPresetOverride = 0;
    float gainAtRequest = 1;
    double requestDelaySeconds = 0;
    GainPreview::Settings previewSettings;
    PreviewSnapshot previewAtRequest, previewAtCapture;
    bool autoGainEnabledAtRequest = false, autoGainEnabledAtCapture = false, autoGainControlledAtCapture = false;
    float manualGain = 1, effectiveGainAtRequest = 1;
    uint64_t autoGainChanges = 0;
};
// Only fixed field names and numeric values are serialized. No user text or
// paths, no locale-dependent decimals, and no invalid JSON NaN/Inf literals.
inline std::string MetadataJson(const Metadata& m)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(9)
        << "{\n  \"schema\": 2,\n  \"edit_stage\": \"after_positive_gain_before_colour_and_clipping\","
        << "\n  \"guide_lattice\": \"" << (m.originalGuides ? "transport_matched" : "private_dlss_input")
        << "\",\n  \"guide_origin\": [0, 0],"
           "\n  \"motion_units\": \"private_input_pixels\",\n  \"mv_scale\": [1, 1]";
    auto value = [&](const char* name, auto v)
    {
        out << ",\n  \"" << name << "\": ";
        if constexpr (std::is_floating_point_v<decltype(v)>)
        {
            if (!std::isfinite(v))
            {
                out << "null";
                return;
            }
        }
        out << v;
    };
    value("guides_included", m.guidesIncluded);
    value("depth_included", m.depthIncluded || m.guidesIncluded);
    value("original_guides_active", m.originalGuides);
    value("captured_guides_match_private_dlss_inputs", !m.originalGuides);
    value("private_depth_allocation_width", m.dlssDepthW);
    value("private_depth_allocation_height", m.dlssDepthH);
    value("private_motion_allocation_width", m.dlssMotionW);
    value("private_motion_allocation_height", m.dlssMotionH);
    value("private_depth_origin_x", m.dlssDepthX);
    value("private_depth_origin_y", m.dlssDepthY);
    value("private_motion_origin_x", m.dlssMotionX);
    value("private_motion_origin_y", m.dlssMotionY);
    value("private_motion_scale_x", m.dlssMotionScaleX);
    value("private_motion_scale_y", m.dlssMotionScaleY);
    value("depth_weighted_positive_gain", m.depthTransport.weightedGain);
    value("depth_additive_brightness", m.depthTransport.additive);
    value("depth_brightness_full_range", m.depthTransport.fullRange);
    value("depth_brightness_near", m.depthTransport.nearLimit);
    value("depth_brightness_far", m.depthTransport.farLimit);
    value("depth_brightness_curve", m.depthTransport.curve);
    value("depth_brightness_strength", m.depthTransport.bias);
    value("auto_positive_gain_enabled_at_request", m.autoGainEnabledAtRequest);
    value("auto_positive_gain_enabled_at_capture", m.autoGainEnabledAtCapture);
    value("auto_positive_gain_controlled_capture", m.autoGainControlledAtCapture);
    value("manual_positive_gain", m.manualGain);
    value("effective_positive_gain_at_request", m.effectiveGainAtRequest);
    value("auto_positive_gain_changes_primary", m.autoGainChanges);
    value("positive_gain_at_request", m.gainAtRequest);
    value("private_preset_at_request", m.privatePresetAtRequest);
    value("capture_request_delay_seconds", m.requestDelaySeconds);
    value("gain_preview_enabled_at_request", m.previewEnabledAtRequest);
    value("gain_preview_enabled_at_capture", m.previewEnabledAtCapture);
    value("gain_preview_epsilon", GainPreview::Epsilon);
    const auto methodSettings = GainPreview::Sanitize(m.previewSettings);
    value("gain_preview_method_at_capture", methodSettings.method);
    value("gain_preview_boundary_target_at_capture", methodSettings.boundaryTarget);
    value("gain_preview_target_at_capture", methodSettings.method == GainPreview::DepthBoundary
                                                ? methodSettings.boundaryTarget
                                                : GainPreview::Target(m.privatePreset == 10 || m.privatePreset == 11));
    const auto settings = GainPreview::Sanitize(m.previewSettings);
    value("gain_preview_require_changing_motion", settings.requireChange);
    value("gain_preview_slow_seconds", settings.slowSeconds);
    value("gain_preview_fast_seconds", settings.fastSeconds);
    value("gain_preview_fast_up_seconds", settings.fastSeconds);
    value("gain_preview_fast_down_seconds", settings.downSeconds);
    value("gain_preview_boundary_use_motion", settings.boundaryUseMotion);
    value("gain_preview_boundary_motion_gate", settings.boundaryMotionGate);
    value("gain_preview_boundary_min_pairs", settings.boundaryMinPairs);
    value("gain_preview_motion_threshold", settings.motionThreshold);
    auto preview = [&](const char* prefix, const PreviewSnapshot& s)
    {
        const std::string p = prefix;
        auto field = [&](const char* key, auto v) { value((p + key).c_str(), v); };
        field("method", s.method);
        field("boundary_pairs", s.boundaryPairs);
        field("boundary_base", s.boundaryBase);
        field("boundary_slope", s.boundarySlope);
        field("boundary_target", s.boundaryTarget);
        field("valid", s.valid);
        field("paused", s.paused);
        field("catching_up", s.catchingUp);
        field("suggested_gain", s.valid ? s.suggestedGain : NAN);
        field("raw_gain", s.valid ? s.rawGain : NAN);
        field("measurement_age_seconds", s.ageSeconds >= 0 ? s.ageSeconds : NAN);
        field("measurements", s.measurements);
        field("moving_percent", s.movingPercent);
        field("changed_percent", s.changedPercent);
    };
    preview("preview_request_", m.previewAtRequest);
    preview("preview_capture_", m.previewAtCapture);
    value("frame", m.frame);
    value("submission_epoch", m.submissionEpoch);
    value("width", m.width);
    value("height", m.height);
    value("positive_gain", m.gain);
    value("edit_saturation", m.saturation);
    value("depth_colour_strength", m.depthStrength);
    value("full_nr_answer", m.fullNrAnswer);
    value("private_preset", m.privatePreset);
    value("private_cnn_preset_override", m.cnnPresetOverride);
    value("private_transformer_preset_override", m.transformerPresetOverride);
    value("nr_preset_setting", m.nrPresetSetting);
    value("private_is_hdr", m.privateIsHdr);
    value("private_exposure_mode", m.exposureMode);
    value("private_manual_exposure", m.manualExposure);
    value("depth_inverted", m.invertedDepth);
    value("packed", m.packed);
    value("private_history_reset", m.reset);
    value("held", m.held);
    value("expected_pass_mask", m.expectedPassMask);
    value("completed_pass_mask_before_capture", m.completedPassMask);
    value("nr_white_point_source", m.whitePointSource);
    value("nr_auto_trim_control", m.autoTrimControl);
    value("nr_game_trim_control", m.gameTrimControl);
    value("nr_scan_trim_control", m.scanTrimControl);
    // Trim/anchor/exposure controls are not a readback of the applied GPU white point.
    out << ",\n  \"nr_gpu_white_point_included\": false\n}\n";
    return out.str();
}
} // namespace DlssNr::EditDump
