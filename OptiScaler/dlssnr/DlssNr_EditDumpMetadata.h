#pragma once
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>

namespace DlssNr::EditDump
{
struct Metadata
{
    uint64_t frame = 0, submissionEpoch = 0;
    uint32_t width = 0, height = 0, privatePreset = 0, nrPresetSetting = 0, exposureMode = 0;
    uint32_t whitePointSource = 0, expectedPassMask = 0, completedPassMask = 0;
    float gain = 1, saturation = 1, depthStrength = 0, manualExposure = 1;
    float autoTrimControl = 1, gameTrimControl = 1, scanTrimControl = 1;
    bool fullNrAnswer = false, invertedDepth = false, privateIsHdr = true;
    bool packed = false, reset = false, held = false;
};
// Only fixed field names and numeric values are serialized. No user text or
// paths, no locale-dependent decimals, and no invalid JSON NaN/Inf literals.
inline std::string MetadataJson(const Metadata& m)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(9)
        << "{\n  \"schema\": 1,\n  \"edit_stage\": \"after_positive_gain_before_colour_and_clipping\","
           "\n  \"guide_lattice\": \"private_dlss_input\",\n  \"guide_origin\": [0, 0],"
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
    value("frame", m.frame);
    value("submission_epoch", m.submissionEpoch);
    value("width", m.width);
    value("height", m.height);
    value("positive_gain", m.gain);
    value("edit_saturation", m.saturation);
    value("depth_colour_strength", m.depthStrength);
    value("full_nr_answer", m.fullNrAnswer);
    value("private_preset", m.privatePreset);
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
