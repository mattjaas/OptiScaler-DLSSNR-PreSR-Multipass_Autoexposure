from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")

def write(rel, text):
    (ROOT / rel).write_text(text, encoding="utf-8", newline="\n")

def rep(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)

def sub(text, pattern, repl, label, flags=re.S):
    text2, count = re.subn(pattern, repl, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text2

# -----------------------------------------------------------------------------
# Config
# -----------------------------------------------------------------------------
rel = "OptiScaler/Config.h"
s = read(rel)
s = rep(
    s,
    "    //   2  a buffer the scan found, anchored to a white point the user chose once\n",
    "    //   2  a buffer the scan found, anchored to a white point the user chose once\n"
    "    //   3  OptiScaler automatic exposure calculated from the original linear-HDR frame\n",
    "config source comment",
)
s = rep(
    s,
    "    CustomOptional<uint32_t> DlssNrWhitePointSource { 1 };\n\n"
    "    CustomOptional<bool> DlssNrScanMeter { false };\n",
    "    CustomOptional<uint32_t> DlssNrWhitePointSource { 1 };\n\n"
    "    // OptiScaler-owned automatic exposure controls. Automatic exposure deliberately ignores\n"
    "    // the game's ExposureTexture and uses the original linear-HDR frame.\n"
    "    CustomOptional<float> DlssNrAutoExposureTrim { 5.0f };\n"
    "    CustomOptional<float> DlssNrAutoExposureShadowProtection { 100.0f };\n\n"
    "    // Base-white-point-dependent Trim calibration tables, serialized as baseWhitePoint:trim pairs.\n"
    "    CustomOptional<std::string> DlssNrGameExposureTrimAnchors { std::string() };\n"
    "    CustomOptional<std::string> DlssNrAutoExposureTrimAnchors { std::string() };\n\n"
    "    // Calibration preview only; deliberately not persisted.\n"
    "    CustomOptional<bool> DlssNrGameExposureTrimPreview { false };\n"
    "    CustomOptional<bool> DlssNrAutoExposureTrimPreview { false };\n\n"
    "    CustomOptional<bool> DlssNrScanMeter { false };\n",
    "config auto exposure fields",
)
write(rel, s)

rel = "OptiScaler/Config.cpp"
s = read(rel)
s = rep(
    s,
    "            DlssNrWhitePointSource.set_from_config(readUInt(\"DlssNr\", \"WhitePointSource\"));\n",
    "            DlssNrWhitePointSource.set_from_config(readUInt(\"DlssNr\", \"WhitePointSource\"));\n"
    "            DlssNrAutoExposureTrim.set_from_config(readFloat(\"DlssNr\", \"AutoExposureTrim\"));\n"
    "            DlssNrAutoExposureShadowProtection.set_from_config(readFloat(\"DlssNr\", \"AutoExposureShadowProtection\"));\n"
    "            DlssNrGameExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"GameExposureTrimAnchors\"));\n"
    "            DlssNrAutoExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"AutoExposureTrimAnchors\"));\n",
    "config read auto exposure",
)
s = rep(
    s,
    "    ini.SetValue(\"DlssNr\", \"WhitePointTrim\", GetFloatValue(Instance()->DlssNrWhitePointTrim.value_for_config()).c_str());\n",
    "    ini.SetValue(\"DlssNr\", \"WhitePointTrim\", GetFloatValue(Instance()->DlssNrWhitePointTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrim\", GetFloatValue(Instance()->DlssNrAutoExposureTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"AutoExposureShadowProtection\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureShadowProtection.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"GameExposureTrimAnchors\",\n"
    "                 Instance()->DlssNrGameExposureTrimAnchors.value_for_config_or(\"\").c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrimAnchors\",\n"
    "                 Instance()->DlssNrAutoExposureTrimAnchors.value_for_config_or(\"\").c_str());\n",
    "config write auto exposure",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Shared status: publish both game and OptiScaler automatic exposure.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Status.h"
s = read(rel)
s = rep(
    s,
    "    ExposureStatus exposure;\n"
    "    bool captureInProgress = false;\n",
    "    ExposureStatus exposure;\n"
    "    ExposureStatus autoExposure;\n"
    "    bool captureInProgress = false;\n",
    "status snapshot auto exposure",
)
s = rep(
    s,
    "ExposureStatus GameExposureStatus();\n",
    "ExposureStatus GameExposureStatus();\n"
    "ExposureStatus AutoExposureStatus();\n",
    "status d3d auto declaration",
)
s = rep(
    s,
    "bool ExposureOfferedVk();\n",
    "bool ExposureOfferedVk();\n"
    "ExposureStatus GameExposureStatusVk();\n"
    "ExposureStatus AutoExposureStatusVk();\n",
    "status Vulkan exposure declarations",
)
write(rel, s)

rel = "OptiScaler/dlssnr/DlssNr_Status.cpp"
s = read(rel)
s = rep(
    s,
    "ExposureStatus GameExposureStatus() { return ReadStatus(Backend::Dx12).exposure; }\n",
    "ExposureStatus GameExposureStatus() { return ReadStatus(Backend::Dx12).exposure; }\n"
    "ExposureStatus AutoExposureStatus() { return ReadStatus(Backend::Dx12).autoExposure; }\n",
    "status d3d auto getter",
)
s = rep(
    s,
    "bool ExposureOfferedVk() { return ReadStatus(Backend::Vulkan).exposure.offeredNow; }\n",
    "bool ExposureOfferedVk() { return ReadStatus(Backend::Vulkan).exposure.offeredNow; }\n"
    "ExposureStatus GameExposureStatusVk() { return ReadStatus(Backend::Vulkan).exposure; }\n"
    "ExposureStatus AutoExposureStatusVk() { return ReadStatus(Backend::Vulkan).autoExposure; }\n",
    "status Vulkan getters",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Menu helpers and controls.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_MenuInput.cpp"
s = read(rel)
s = rep(s, "#include <cstdio>\n", "#include <cstdio>\n#include <string>\n#include <vector>\n", "menu includes")
helper_marker = "void RenderInput(Config* config, float menuResScale)\n"
helper = r'''struct ExposureTrimAnchorUi
{
    float key = 0.0f;
    float trim = 1.0f;
};

static std::vector<ExposureTrimAnchorUi> ParseExposureTrimAnchorsUi(const std::string& text)
{
    std::vector<ExposureTrimAnchorUi> out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token = text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;
        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;
        try
        {
            const float key = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));
            if (std::isfinite(key) && key > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ key, std::clamp(trim, 0.25f, 50.0f) });
        }
        catch (...) {}
    }
    std::sort(out.begin(), out.end(),
              [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b) { return a.key < b.key; });
    return out;
}

static std::string SerializeExposureTrimAnchorsUi(const std::vector<ExposureTrimAnchorUi>& anchors)
{
    std::string out;
    char buf[64];
    for (const auto& p : anchors)
    {
        snprintf(buf, sizeof(buf), "%.7g:%.7g;", p.key, p.trim);
        out += buf;
    }
    return out;
}

static bool UpsertExposureTrimAnchorUi(std::vector<ExposureTrimAnchorUi>& anchors, float key, float trim)
{
    if (!(std::isfinite(key) && key > 1e-8f))
        return false;
    trim = std::clamp(trim, 0.25f, 50.0f);
    for (auto& p : anchors)
    {
        if (key > p.key * 0.98f && key < p.key * 1.02f)
        {
            p = { key, trim };
            std::sort(anchors.begin(), anchors.end(),
                      [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b) { return a.key < b.key; });
            return true;
        }
    }
    if (anchors.size() >= 8)
        return false;
    anchors.push_back({ key, trim });
    std::sort(anchors.begin(), anchors.end(),
              [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b) { return a.key < b.key; });
    return true;
}

static float ExposureTrimForUi(float key, float fallback,
                               const std::vector<ExposureTrimAnchorUi>& anchors, bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 50.0f);
    if (preview || anchors.empty() || !(std::isfinite(key) && key > 1e-8f))
        return fallback;
    if (anchors.size() == 1)
        return anchors[0].trim;
    if (key <= anchors.front().key)
        return anchors.front().trim;
    if (key >= anchors.back().key)
        return anchors.back().trim;
    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];
        if (key >= a.key && key <= b.key && b.key > a.key * 1.000001f)
        {
            const float t = (std::log(key) - std::log(a.key)) / (std::log(b.key) - std::log(a.key));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 50.0f);
        }
    }
    return anchors.back().trim;
}

static void RenderExposureTrimAnchorControls(CustomOptional<std::string>& storedAnchors,
                                             CustomOptional<bool>& previewSetting, float baseWhitePoint,
                                             float sliderTrim, const char* idSuffix)
{
    auto anchors = ParseExposureTrimAnchorsUi(storedAnchors.value_or_default());
    const bool haveKey = std::isfinite(baseWhitePoint) && baseWhitePoint > 1e-8f;
    const std::string addLabel = std::string("Add Anchor point##") + idSuffix;
    ImGui::BeginDisabled(!haveKey);
    if (ImGui::Button(addLabel.c_str()))
    {
        if (UpsertExposureTrimAnchorUi(anchors, baseWhitePoint, sliderTrim))
            storedAnchors = SerializeExposureTrimAnchorsUi(anchors);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    bool preview = previewSetting.value_or_default();
    const std::string previewLabel = std::string("Preview Trim value for actual scene##") + idSuffix;
    if (ImGui::Checkbox(previewLabel.c_str(), &preview))
        previewSetting = preview;

    HelpMarker("When enabled, Anchor points are temporarily ignored and the Trim slider is applied"
               "\ndirectly. Tune the current scene, then press Add Anchor point."
               "\nThe calibration axis is Base White Point = PreExposure / Exposure."
               "\nThe preview switch is intentionally not saved across restarts.");

    if (!haveKey)
        ImGui::TextDisabled("Waiting for a valid base white point before an Anchor point can be added.");
    else
    {
        const float effective = ExposureTrimForUi(baseWhitePoint, sliderTrim, anchors, preview);
        ImGui::TextDisabled("Current base white point %.5f -> effective Trim %.2fx%s",
                            baseWhitePoint, effective, preview ? " (preview)" : "");
    }

    if (anchors.empty())
    {
        ImGui::TextDisabled("No Trim Anchor points: the Trim slider is used for every base white point.");
        return;
    }

    if (anchors.size() == 1)
        ImGui::TextDisabled("1 Trim Anchor point: its Trim is used for every base white point.");
    else
        ImGui::TextDisabled("%u Trim Anchor points: Trim is interpolated between base white-point values.",
                            (unsigned int) anchors.size());

    ImGui::PushID(idSuffix);
    for (size_t i = 0; i < anchors.size(); ++i)
    {
        ImGui::PushID((int) i);
        if (ImGui::SmallButton("x"))
        {
            anchors.erase(anchors.begin() + i);
            storedAnchors = SerializeExposureTrimAnchorsUi(anchors);
            ImGui::PopID();
            --i;
            continue;
        }
        ImGui::SameLine();
        ImGui::Text("Base white point %.5f -> Trim %.2fx", anchors[i].key, anchors[i].trim);
        ImGui::PopID();
    }
    ImGui::PopID();
}

'''
s = rep(s, helper_marker, helper + helper_marker, "menu helper insertion")
s = rep(
    s,
    '            static const char* sourceNames[] = { "Manual paper white", "Game exposure",\n'
    '                                                 "Scanned exposure (experimental)" };\n',
    '            static const char* sourceNames[] = { "Manual paper white", "The game\\\'s own exposure",\n'
    '                                                 "A buffer the scan found", "Automatic exposure" };\n',
    "menu source names",
)
s = rep(s, "            if (source < 0 || source > 2)\n", "            if (source < 0 || source > 3)\n", "menu source range")
s = rep(
    s,
    '            HelpMarker("Game exposure uses supplied data. Scanned exposure requires calibration and may select the wrong buffer.");\n',
    '            HelpMarker("The game\\\'s own exposure uses the ExposureTexture supplied to the upscaler. A buffer the scan found uses manual calibration. Automatic exposure is calculated by OptiScaler from the original linear-HDR frame.");\n',
    "menu source help",
)
s = rep(
    s,
    "            const auto ex = DlssNr::GameExposureStatus();\n"
    "            const bool vk = DlssNr::IsRunningVk();\n"
    "            const bool haveExposure = vk ? DlssNr::ExposureOfferedVk() : ex.everOffered;\n",
    "            const bool vk = DlssNr::IsRunningVk();\n"
    "            const auto ex = vk ? DlssNr::GameExposureStatusVk() : DlssNr::GameExposureStatus();\n"
    "            const auto autoEx = vk ? DlssNr::AutoExposureStatusVk() : DlssNr::AutoExposureStatus();\n"
    "            const bool haveExposure = vk ? DlssNr::ExposureOfferedVk() : ex.everOffered;\n",
    "menu status selection",
)
game_status_old = r'''                else if (vk)
                    ImGui::TextDisabled("Using game exposure.");
                else if (ex.exposure > 1e-6f)
                {
                    const float trim = std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
                    ImGui::TextDisabled("Game exposure %.4f  ->  white point %.2f%s", ex.exposure,
                                        ex.preExposure / ex.exposure * trim,
                                        ex.offeredNow ? "" : "  (held: absent this frame)");
                }
                else
                    ImGui::TextDisabled("Reading exposure...");
'''
game_status_new = r'''                else if (ex.exposure > 1e-6f)
                {
                    const float baseWhitePoint = ex.preExposure / ex.exposure;
                    const auto anchors =
                        ParseExposureTrimAnchorsUi(config->DlssNrGameExposureTrimAnchors.value_or_default());
                    const float trim = ExposureTrimForUi(
                        baseWhitePoint, config->DlssNrWhitePointTrim.value_or_default(), anchors,
                        config->DlssNrGameExposureTrimPreview.value_or_default());
                    ImGui::TextDisabled("Game exposure %.4f  ->  white point %.2f%s", ex.exposure,
                                        baseWhitePoint * trim,
                                        ex.offeredNow ? "" : "  (held: absent this frame)");
                }
                else
                    ImGui::TextDisabled("Reading exposure...");
'''
s = rep(s, game_status_old, game_status_new, "menu game status")
s = rep(
    s,
    "            else if (haveExposure)\n"
    "            {\n"
    "                ImGui::TextDisabled(\"Game exposure is available.\");\n"
    "            }\n",
    "            else if (source == 3)\n"
    "            {\n"
    "                if (autoEx.exposure > 1e-8f)\n"
    "                {\n"
    "                    const float baseWhitePoint = autoEx.preExposure / autoEx.exposure;\n"
    "                    const auto anchors = ParseExposureTrimAnchorsUi(\n"
    "                        config->DlssNrAutoExposureTrimAnchors.value_or_default());\n"
    "                    const float trim = ExposureTrimForUi(\n"
    "                        baseWhitePoint, config->DlssNrAutoExposureTrim.value_or_default(), anchors,\n"
    "                        config->DlssNrAutoExposureTrimPreview.value_or_default());\n"
    "                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),\n"
    "                                       \"Automatic exposure %.4f  ->  white point %.2f\", autoEx.exposure,\n"
    "                                       baseWhitePoint * trim);\n"
    "                    ImGui::TextDisabled(\"Calculated by OptiScaler; the game's ExposureTexture is ignored.\");\n"
    "                }\n"
    "                else\n"
    "                    ImGui::TextDisabled(\"Calculating automatic exposure...\");\n"
    "            }\n"
    "            else if (haveExposure)\n"
    "            {\n"
    "                ImGui::TextDisabled(\"Game exposure is available.\");\n"
    "            }\n",
    "menu automatic status",
)
trim_pattern = r'''        else if \(wpSource == 1\)\n        \{\n[\s\S]*?        \}\n        else\n        \{\n            float wpScale'''
trim_replacement = r'''        else if (wpSource == 1)
        {
            float gameTrim = config->DlssNrWhitePointTrim.value_or_default();
            if (ImGui::SliderFloat("Trim (x the game's exposure)", &gameTrim, 0.25f, 50.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointTrim = std::clamp(gameTrim, 0.25f, 50.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##wptrim"))
            {
                config->DlssNrWhitePointTrim = 1.0f;
                gameTrim = 1.0f;
            }

            HelpMarker("1.00x uses the game's value unchanged. Range: 0.25x to 50.00x."
                       "\nTry to use the highest value that subjectively looks best; excessive values"
                       "\nwill degrade image quality. Anchor points can use different Trim values for"
                       "\ndifferent Base White Point values.");

            const auto gameStatus = DlssNr::IsRunningVk() ? DlssNr::GameExposureStatusVk()
                                                           : DlssNr::GameExposureStatus();
            const float baseWhitePoint = gameStatus.exposure > 1e-8f
                ? gameStatus.preExposure / gameStatus.exposure : 0.0f;
            RenderExposureTrimAnchorControls(config->DlssNrGameExposureTrimAnchors,
                                             config->DlssNrGameExposureTrimPreview,
                                             baseWhitePoint, gameTrim, "gameExposureTrim");
        }
        else if (wpSource == 3)
        {
            float autoTrim = config->DlssNrAutoExposureTrim.value_or_default();
            if (ImGui::SliderFloat("Trim (x automatic exposure)", &autoTrim, 0.25f, 50.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrAutoExposureTrim = std::clamp(autoTrim, 0.25f, 50.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##autoexposuretrim"))
            {
                config->DlssNrAutoExposureTrim = 5.0f;
                autoTrim = 5.0f;
            }

            HelpMarker("OptiScaler calculates exposure from the ORIGINAL linear-HDR frame before Neural Rendering."
                       "\nThe game's ExposureTexture is ignored. Range: 0.25x to 50.00x."
                       "\nTry to use the highest value that subjectively looks best; excessive values"
                       "\nwill degrade image quality. Anchor points can use different Trim values for"
                       "\ndifferent Base White Point values."
                       "\nAutomatic exposure is available on D3D12 and Vulkan.");

            float protection = config->DlssNrAutoExposureShadowProtection.value_or_default();
            if (ImGui::SliderFloat("Shadow protection from bright highlights", &protection, 0.0f, 100.0f, "%.0f%%"))
                config->DlssNrAutoExposureShadowProtection = protection;

            HelpMarker("Controls how strongly very bright highlights are prevented from driving Automatic exposure."
                       "\n0% keeps the original full-frame arithmetic average."
                       "\n100% uses the strongest soft highlight compression. No tiles are discarded.");

            ImGui::TextDisabled("Metering: highlight-compressed arithmetic average.");

            const auto autoStatus = DlssNr::IsRunningVk() ? DlssNr::AutoExposureStatusVk()
                                                           : DlssNr::AutoExposureStatus();
            const float baseWhitePoint = autoStatus.exposure > 1e-8f
                ? autoStatus.preExposure / autoStatus.exposure : 0.0f;
            RenderExposureTrimAnchorControls(config->DlssNrAutoExposureTrimAnchors,
                                             config->DlssNrAutoExposureTrimPreview,
                                             baseWhitePoint, autoTrim, "automaticExposureTrim");
        }
        else
        {
            float wpScale'''
s = sub(s, trim_pattern, trim_replacement, "menu trim blocks")
write(rel, s)

# -----------------------------------------------------------------------------
# Shared constants and automatic-exposure mode.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Common.h"
s = read(rel)
s = rep(
    s,
    "    DlssNrMode_ResizePrivateGuides = 10\n",
    "    DlssNrMode_ResizePrivateGuides = 10,\n"
    "    DlssNrMode_AutoExposure = 11\n",
    "automatic exposure mode enum",
)
constant_tail = r'''    float ResidualBlend;
    uint32_t ResidualHistoryValid;
    uint32_t ResidualMotionBaseX;
    uint32_t ResidualMotionBaseY;
};
'''
new_constant_tail = r'''    float ResidualBlend;
    uint32_t ResidualHistoryValid;
    uint32_t ResidualMotionBaseX;
    uint32_t ResidualMotionBaseY;

    // Automatic exposure / exposure-dependent Trim calibration.
    float PreExposure;
    uint32_t ExposureSourceWidth;
    uint32_t ExposureSourceHeight;
    uint32_t MeterCopiesExposure;
    float ExposureTrim;
    uint32_t UseExposureWhitePoint;
    uint32_t ExposureTrimAnchorCount;
    uint32_t ExposureTrimPreview;
    float ExposureTrimAnchorExposure0;
    float ExposureTrimAnchorTrim0;
    float ExposureTrimAnchorExposure1;
    float ExposureTrimAnchorTrim1;
    float ExposureTrimAnchorExposure2;
    float ExposureTrimAnchorTrim2;
    float ExposureTrimAnchorExposure3;
    float ExposureTrimAnchorTrim3;
    float ExposureTrimAnchorExposure4;
    float ExposureTrimAnchorTrim4;
    float ExposureTrimAnchorExposure5;
    float ExposureTrimAnchorTrim5;
    float ExposureTrimAnchorExposure6;
    float ExposureTrimAnchorTrim6;
    float ExposureTrimAnchorExposure7;
    float ExposureTrimAnchorTrim7;
    float AutoExposureShadowProtection;
};
'''
s = rep(s, constant_tail, new_constant_tail, "automatic exposure constants")
write(rel, s)

# -----------------------------------------------------------------------------
# D3D12 state and runtime helpers.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_ModelState.h"
s = read(rel)
s = rep(
    s,
    "    ID3D12Resource* meter = nullptr;\n"
    "    ID3D12Resource* meterReadback[4] = {};\n",
    "    ID3D12Resource* meter = nullptr;\n"
    "    ID3D12Resource* meterReadback[4] = {};\n"
    "    ID3D12Resource* autoExposure = nullptr;\n"
    "    bool autoExposureReadable = false;\n"
    "    float autoExposureValue = 0.0f;\n"
    "    float autoExposurePreExposure = 1.0f;\n"
    "    unsigned long long autoExposureFrames = 0;\n"
    "    uint32_t exposureReadbackSource = 0;\n",
    "d3d state auto exposure",
)
s = rep(
    s,
    "    bool meterExposureValid[4] = {};\n",
    "    uint32_t meterExposureKind[4] = {}; // 0 none, 1 game, 2 automatic\n"
    "    float meterExposurePreExposure[4] = {};\n",
    "d3d meter kind",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_State.h"
s = read(rel)
s = rep(
    s,
    "    void CopyMeterToReadback(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, bool exposureBound);\n",
    "    void CopyMeterToReadback(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, bool exposureBound);\n"
    "    void CopyAutoExposureToReadback(ID3D12GraphicsCommandList* cmdList, float preExposure);\n",
    "d3d auto readback declaration",
)
s = rep(
    s,
    "    float ResolveWhitePoint(const Config& cfg, bool isHdrBuffer);\n",
    "    float ResolveWhitePoint(const Config& cfg, bool isHdrBuffer);\n"
    "    void FillExposureTrimConstants(DlssNrConstants& params, const Config& cfg, uint32_t source);\n",
    "d3d trim helper declaration",
)
s = rep(
    s,
    "        float whitePoint = 1.0f, exposurePreMul = 0.0f;\n"
    "        unsigned int useGameExposure = 0;\n"
    "        ID3D12Resource* exposureTex = nullptr;\n",
    "        float whitePoint = 1.0f, exposurePreMul = 0.0f;\n"
    "        unsigned int useGameExposure = 0;\n"
    "        uint32_t whitePointSource = 0;\n"
    "        bool usingAutoExposure = false;\n"
    "        ID3D12Resource* exposureTex = nullptr;\n",
    "d3d encode context fields",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Models.cpp"
s = read(rel)
s = rep(
    s,
    "        if (nr.meter != nullptr)\n"
    "            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);\n"
    "    }\n\n"
    "    if (!nr.output || !nr.colorCopy || !nr.hdrCopy)\n",
    "        if (nr.meter != nullptr)\n"
    "            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);\n"
    "    }\n\n"
    "    if (nr.autoExposure == nullptr)\n"
    "    {\n"
    "        nr.autoExposure = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1);\n"
    "        nr.autoExposureReadable = false;\n"
    "        if (nr.autoExposure != nullptr)\n"
    "            LOG_INFO(\"DLSS-NR: GPU automatic exposure is available\");\n"
    "        else\n"
    "            LOG_WARN(\"DLSS-NR: could not allocate the automatic exposure texture\");\n"
    "    }\n\n"
    "    if (!nr.output || !nr.colorCopy || !nr.hdrCopy)\n",
    "d3d auto exposure allocation",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Resources.cpp"
s = read(rel)
s = rep(
    s,
    "    if (nr.meter != nullptr)\n"
    "    {\n"
    "        ParkNrResource(nr.meter);\n"
    "    }\n\n"
    "    if (nr.calib != nullptr)\n",
    "    if (nr.meter != nullptr)\n"
    "    {\n"
    "        ParkNrResource(nr.meter);\n"
    "    }\n"
    "    if (nr.autoExposure != nullptr)\n"
    "        ParkNrResource(nr.autoExposure);\n"
    "    nr.autoExposureReadable = false;\n"
    "    nr.autoExposureValue = 0.0f;\n"
    "    nr.autoExposurePreExposure = 1.0f;\n"
    "    nr.autoExposureFrames = 0;\n"
    "    nr.exposureReadbackSource = 0;\n\n"
    "    if (nr.calib != nullptr)\n",
    "d3d auto exposure release",
)
s = rep(
    s,
    "    for (bool& valid : nr.meterExposureValid)\n"
    "        valid = false;\n",
    "    for (uint32_t& kind : nr.meterExposureKind)\n"
    "        kind = 0u;\n",
    "d3d meter kind release",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Exposure.cpp"
s = read(rel)
s = rep(s, '#include "DlssNr_Dx12_State.h"\n', '#include "DlssNr_Dx12_State.h"\n#include <vector>\n', "d3d exposure vector include")
s = rep(
    s,
    "    nr.meterExposureValid[slot] = exposureBound;\n",
    "    nr.meterExposureKind[slot] = exposureBound ? 1u : 0u;\n"
    "    nr.meterExposurePreExposure[slot] = nr.gamePreExposure;\n",
    "d3d game readback kind",
)
copy_auto_marker = "auto DlssNr_Dx12::State::ConsumeCalibrationReadback() -> void\n"
copy_auto_code = r'''auto DlssNr_Dx12::State::CopyAutoExposureToReadback(ID3D12GraphicsCommandList* cmdList, float preExposure) -> void
{
    if (nr.autoExposure == nullptr)
        return;
    const unsigned int slot = (unsigned int) (nr.meterFrames % 4);
    ID3D12Resource* buffer = nr.meterReadback[slot];
    if (buffer == nullptr)
        return;

    nr.meterExposureKind[slot] = 2u;
    nr.meterExposurePreExposure[slot] =
        std::isfinite(preExposure) && preExposure > 1e-6f ? preExposure : 1.0f;

    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = nr.autoExposure;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = buffer;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
    dst.PlacedFootprint.Footprint.Width = 1;
    dst.PlacedFootprint.Footprint.Height = 1;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = kMeterRowBytes;

    Barrier(cmdList, nr.autoExposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(cmdList, nr.autoExposure, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    nr.meterFrames++;
    nr.autoExposureFrames++;
}

'''
s = rep(s, copy_auto_marker, copy_auto_code + copy_auto_marker, "d3d auto readback helper")
s = rep(
    s,
    "    if (nr.meterExposureValid[slot] && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "        nr.gameExposure = src[0];\n",
    "    if (nr.meterExposureKind[slot] == 1u && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "        nr.gameExposure = src[0];\n"
    "    else if (nr.meterExposureKind[slot] == 2u && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "    {\n"
    "        nr.autoExposureValue = src[0];\n"
    "        nr.autoExposurePreExposure = nr.meterExposurePreExposure[slot];\n"
    "    }\n",
    "d3d consume both exposure kinds",
)
s = rep(
    s,
    "    nr.gameExposure = 0.0f;\n\n"
    "    for (bool& valid : nr.meterExposureValid)\n"
    "        valid = false;\n",
    "    nr.gameExposure = 0.0f;\n"
    "    nr.autoExposureValue = 0.0f;\n"
    "    nr.autoExposurePreExposure = 1.0f;\n\n"
    "    for (uint32_t& kind : nr.meterExposureKind)\n"
    "        kind = 0u;\n",
    "d3d invalidate both exposures",
)
runtime_helpers = r'''namespace
{
struct TrimAnchorRuntime
{
    float key = 0.0f;
    float trim = 1.0f;
};

std::vector<TrimAnchorRuntime> ParseTrimAnchors(const std::string& text)
{
    std::vector<TrimAnchorRuntime> out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token = text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;
        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;
        try
        {
            const float key = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));
            if (std::isfinite(key) && key > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ key, std::clamp(trim, 0.25f, 50.0f) });
        }
        catch (...) {}
    }
    std::sort(out.begin(), out.end(), [](const TrimAnchorRuntime& a, const TrimAnchorRuntime& b) { return a.key < b.key; });
    return out;
}

float TrimForKey(float key, float fallback, const std::vector<TrimAnchorRuntime>& anchors, bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 50.0f);
    if (preview || anchors.empty() || !(std::isfinite(key) && key > 1e-8f))
        return fallback;
    if (anchors.size() == 1)
        return anchors[0].trim;
    if (key <= anchors.front().key)
        return anchors.front().trim;
    if (key >= anchors.back().key)
        return anchors.back().trim;
    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];
        if (key >= a.key && key <= b.key && b.key > a.key * 1.000001f)
        {
            const float t = (std::log(key) - std::log(a.key)) / (std::log(b.key) - std::log(a.key));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 50.0f);
        }
    }
    return anchors.back().trim;
}
}

void DlssNr_Dx12::State::FillExposureTrimConstants(DlssNrConstants& params, const Config& cfg, uint32_t source)
{
    const bool automatic = source == 3;
    const float fallback = automatic ? cfg.DlssNrAutoExposureTrim.value_or_default()
                                     : cfg.DlssNrWhitePointTrim.value_or_default();
    const bool preview = automatic ? cfg.DlssNrAutoExposureTrimPreview.value_or_default()
                                   : cfg.DlssNrGameExposureTrimPreview.value_or_default();
    const auto anchors = ParseTrimAnchors(automatic ? cfg.DlssNrAutoExposureTrimAnchors.value_or_default()
                                                   : cfg.DlssNrGameExposureTrimAnchors.value_or_default());

    params.ExposureTrim = std::clamp(fallback, 0.25f, 50.0f);
    params.ExposureTrimPreview = preview ? 1u : 0u;
    params.ExposureTrimAnchorCount = (uint32_t) std::min<size_t>(anchors.size(), 8);
    float* pairs = &params.ExposureTrimAnchorExposure0;
    for (size_t i = 0; i < params.ExposureTrimAnchorCount; ++i)
    {
        pairs[i * 2 + 0] = anchors[i].key;
        pairs[i * 2 + 1] = anchors[i].trim;
    }
    params.AutoExposureShadowProtection =
        std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);
}

'''
s = rep(s, "auto DlssNr_Dx12::State::ResolveWhitePoint(const Config& cfg, bool isHdrBuffer) -> float\n",
        runtime_helpers + "auto DlssNr_Dx12::State::ResolveWhitePoint(const Config& cfg, bool isHdrBuffer) -> float\n",
        "d3d trim helpers")
s = rep(
    s,
    "        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);\n\n"
    "        return std::clamp(nr.gamePreExposure / nr.gameExposure * trim, 0.01f, 4096.0f);\n",
    "        const float baseWhitePoint = nr.gamePreExposure / nr.gameExposure;\n"
    "        const auto anchors = ParseTrimAnchors(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float trim = TrimForKey(baseWhitePoint, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "                                      cfg.DlssNrGameExposureTrimPreview.value_or_default());\n"
    "        return std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n",
    "d3d game runtime anchors",
)
s = rep(
    s,
    "    // Otherwise the slider, and only the slider.\n",
    "    if (cfg.DlssNrWhitePointSource.value_or_default() == 3 && nr.autoExposureValue > 1e-8f)\n"
    "    {\n"
    "        const float baseWhitePoint = nr.autoExposurePreExposure / nr.autoExposureValue;\n"
    "        const auto anchors = ParseTrimAnchors(cfg.DlssNrAutoExposureTrimAnchors.value_or_default());\n"
    "        const float trim = TrimForKey(baseWhitePoint, cfg.DlssNrAutoExposureTrim.value_or_default(), anchors,\n"
    "                                      cfg.DlssNrAutoExposureTrimPreview.value_or_default());\n"
    "        return std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n"
    "    }\n\n"
    "    // Otherwise the slider, and only the slider.\n",
    "d3d auto fallback white point",
)
write(rel, s)

# -----------------------------------------------------------------------------
# D3D12 encode/resolve: generate the same-frame 1x1 exposure and bind it as t4.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp"
s = read(rel)
s = rep(
    s,
    "    auto& useGameExposure = context.useGameExposure;\n"
    "    auto& exposurePreMul = context.exposurePreMul;\n",
    "    auto& useGameExposure = context.useGameExposure;\n"
    "    auto& exposurePreMul = context.exposurePreMul;\n"
    "    auto& whitePointSource = context.whitePointSource;\n"
    "    auto& usingAutoExposure = context.usingAutoExposure;\n",
    "d3d encode source refs",
)
s = rep(
    s,
    "    const bool exposureSettingOn = cfg.DlssNrWhitePointSource.value_or_default() == 1;\n",
    "    whitePointSource = cfg.DlssNrWhitePointSource.value_or_default();\n"
    "    if (whitePointSource != nr.exposureReadbackSource)\n"
    "    {\n"
    "        InvalidateExposureMeter();\n"
    "        nr.exposureReadbackSource = whitePointSource;\n"
    "    }\n"
    "    const bool exposureSettingOn = whitePointSource == 1;\n",
    "d3d exposure source switching",
)
s = rep(
    s,
    "        meterParams.Width = 1;\n"
    "        meterParams.Height = 1;\n",
    "        meterParams.Width = 1;\n"
    "        meterParams.Height = 1;\n"
    "        meterParams.MeterCopiesExposure = 1;\n",
    "d3d game meter copy flag",
)
auto_dispatch_marker = "    nr.gamePreExposure = frame.PreExposure;\n\n"
auto_dispatch = r'''    if (whitePointSource == 3 && isHdrBuffer && nr.meter != nullptr && nr.autoExposure != nullptr)
    {
        DlssNrConstants meterParams {};
        meterParams.Mode = DlssNrMode_Meter;
        meterParams.Width = kDlssNrMeterGrid;
        meterParams.Height = kDlssNrMeterGrid;
        meterParams.MeterCopiesExposure = 0;

        const D3D12_RESOURCE_STATES priorTargetState = targetState;
        TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        shader.DispatchPass(cmdList, meterParams, target, nullptr, nullptr, nullptr, nullptr, nr.meter, nullptr);
        TransitionTarget(priorTargetState);

        Barrier(cmdList, nr.meter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (nr.autoExposureReadable)
            Barrier(cmdList, nr.autoExposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        DlssNrConstants autoParams {};
        autoParams.Mode = DlssNrMode_AutoExposure;
        autoParams.Width = 1;
        autoParams.Height = 1;
        autoParams.PreExposure = frame.PreExposure;
        autoParams.ExposureSourceWidth = width;
        autoParams.ExposureSourceHeight = height;
        autoParams.AutoExposureShadowProtection =
            std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);

        shader.DispatchPass(cmdList, autoParams, nr.meter, nullptr, nullptr, nullptr, nullptr,
                            nr.autoExposure, nullptr);

        Barrier(cmdList, nr.meter, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.autoExposure, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        nr.autoExposureReadable = true;
        usingAutoExposure = true;
        exposureTex = nr.autoExposure;

        CopyAutoExposureToReadback(cmdList, frame.PreExposure);
        ConsumeMeterReadback();
    }

'''
s = rep(s, auto_dispatch_marker, auto_dispatch + auto_dispatch_marker, "d3d automatic exposure dispatch")
s = rep(
    s,
    "    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && frame.ExposureTexture != nullptr)\n",
    "    if (whitePointSource == 1 && frame.ExposureTexture != nullptr)\n",
    "d3d game source gate",
)
s = rep(
    s,
    "        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);\n"
    "        exposurePreMul = nr.gamePreExposure * trim;\n",
    "        exposurePreMul = nr.gamePreExposure;\n",
    "d3d live game trim moved to shader",
)
s = rep(
    s,
    "    encodeParams.WhitePoint = whitePoint;\n"
    "    encodeParams.UseGameExposure = useGameExposure;\n"
    "    encodeParams.ExposurePreMul = exposurePreMul;\n",
    "    encodeParams.WhitePoint = whitePoint;\n"
    "    encodeParams.UseGameExposure = useGameExposure;\n"
    "    encodeParams.ExposurePreMul = exposurePreMul;\n"
    "    encodeParams.PreExposure = frame.PreExposure;\n"
    "    encodeParams.UseExposureWhitePoint = usingAutoExposure ? 1u : 0u;\n"
    "    FillExposureTrimConstants(encodeParams, cfg, whitePointSource);\n",
    "d3d encode auto constants",
)
s = rep(
    s,
    "    resolveParams.WhitePoint = whitePoint;\n"
    "    resolveParams.UseGameExposure = useGameExposure;\n"
    "    resolveParams.ExposurePreMul = exposurePreMul;\n",
    "    resolveParams.WhitePoint = whitePoint;\n"
    "    resolveParams.UseGameExposure = useGameExposure;\n"
    "    resolveParams.ExposurePreMul = exposurePreMul;\n"
    "    resolveParams.PreExposure = context.frame.PreExposure;\n"
    "    resolveParams.UseExposureWhitePoint = context.usingAutoExposure ? 1u : 0u;\n"
    "    FillExposureTrimConstants(resolveParams, cfg, context.whitePointSource);\n",
    "d3d resolve auto constants",
)
write(rel, s)

# -----------------------------------------------------------------------------
# D3D12 model API: pass only the OptiScaler-owned automatic exposure to NGX.
# Existing game-exposure ownership stays unchanged.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Proxy.h"
s = read(rel)
s = rep(
    s,
    "    unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, unsigned int width,\n",
    "    unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* exposure,\n"
    "                     ID3D12Resource* output, unsigned int width,\n",
    "proxy public exposure arg",
)
write(rel, s)

rel = "OptiScaler/dlssnr/DlssNr_Proxy.cpp"
s = read(rel)
s = rep(
    s,
    "    unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, unsigned int width,\n",
    "    unsigned int Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* exposure,\n"
    "                     ID3D12Resource* output, unsigned int width,\n",
    "proxy impl decl exposure arg",
)
s = rep(
    s,
    "unsigned int Context::Impl::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                                 ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,\n",
    "unsigned int Context::Impl::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                                 ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* exposure,\n"
    "                                 ID3D12Resource* output,\n",
    "proxy impl definition exposure arg",
)
s = rep(
    s,
    "    SetResource(params, \"DLSSNR.MVec\", motion);\n"
    "    SetResource(params, \"DLSSNR.Output\", output);\n",
    "    SetResource(params, \"DLSSNR.MVec\", motion);\n"
    "    SetResource(params, \"DLSSNR.ExposureTexture\", exposure);\n"
    "    SetResource(params, \"DLSSNR.Output\", output);\n",
    "proxy set exposure resource",
)
s = rep(
    s,
    "unsigned int Context::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                          ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, unsigned int width,\n",
    "unsigned int Context::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, ID3D12Resource* color,\n"
    "                          ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* exposure,\n"
    "                          ID3D12Resource* output, unsigned int width,\n",
    "proxy public definition exposure arg",
)
s = rep(
    s,
    "    return _impl->Run(cmdList, device, color, depth, motion, output, width, height, guideWidth, guideHeight,\n",
    "    return _impl->Run(cmdList, device, color, depth, motion, exposure, output, width, height, guideWidth, guideHeight,\n",
    "proxy forward exposure arg",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp"
s = read(rel)
s = rep(
    s,
    "        result = static_cast<int>(nr.models[pass].Run(\n"
    "            cmdList, device, passInput, depthIn, motionIn, passOutput, workWidth, workHeight, guideWidth,\n",
    "        ID3D12Resource* modelExposure = encoded.usingAutoExposure ? encoded.exposureTex : nullptr;\n"
    "        result = static_cast<int>(nr.models[pass].Run(\n"
    "            cmdList, device, passInput, depthIn, motionIn, modelExposure, passOutput, workWidth, workHeight, guideWidth,\n",
    "d3d model automatic exposure",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Status.cpp"
s = read(rel)
s = rep(
    s,
    "          { nr.exposureFrames, nr.exposureOfferedNow, nr.exposureEverOffered, nr.gameExposure, nr.gamePreExposure },\n"
    "          captureFrames.isActive() });\n",
    "          { nr.exposureFrames, nr.exposureOfferedNow, nr.exposureEverOffered, nr.gameExposure, nr.gamePreExposure },\n"
    "          { nr.autoExposureFrames, nr.autoExposureReadable, nr.autoExposureFrames != 0,\n"
    "            nr.autoExposureValue, nr.autoExposurePreExposure },\n"
    "          captureFrames.isActive() });\n",
    "d3d publish auto exposure",
)
write(rel, s)

# -----------------------------------------------------------------------------
# HLSL: exact tile mean, protected automatic exposure, same-frame Trim interpolation.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
s = read(rel)
s = rep(
    s,
    "    float gEnvironmentDetail;\n"
    "    float gEnvironmentColour;\n"
    "};\n",
    "    float gEnvironmentDetail;\n"
    "    float gEnvironmentColour;\n"
    "    // Four fields present in the shared C++ struct for the separate residual shader.\n"
    "    float gResidualBlendPadding;\n"
    "    uint  gResidualHistoryValidPadding;\n"
    "    uint  gResidualMotionBaseXPadding;\n"
    "    uint  gResidualMotionBaseYPadding;\n"
    "    float gPreExposure;\n"
    "    uint  gExposureSourceWidth;\n"
    "    uint  gExposureSourceHeight;\n"
    "    uint  gMeterCopiesExposure;\n"
    "    float gExposureTrim;\n"
    "    uint  gUseExposureWhitePoint;\n"
    "    uint  gExposureTrimAnchorCount;\n"
    "    uint  gExposureTrimPreview;\n"
    "    float gExposureTrimAnchorExposure0;\n"
    "    float gExposureTrimAnchorTrim0;\n"
    "    float gExposureTrimAnchorExposure1;\n"
    "    float gExposureTrimAnchorTrim1;\n"
    "    float gExposureTrimAnchorExposure2;\n"
    "    float gExposureTrimAnchorTrim2;\n"
    "    float gExposureTrimAnchorExposure3;\n"
    "    float gExposureTrimAnchorTrim3;\n"
    "    float gExposureTrimAnchorExposure4;\n"
    "    float gExposureTrimAnchorTrim4;\n"
    "    float gExposureTrimAnchorExposure5;\n"
    "    float gExposureTrimAnchorTrim5;\n"
    "    float gExposureTrimAnchorExposure6;\n"
    "    float gExposureTrimAnchorTrim6;\n"
    "    float gExposureTrimAnchorExposure7;\n"
    "    float gExposureTrimAnchorTrim7;\n"
    "    float gAutoExposureShadowProtection;\n"
    "};\n",
    "HLSL auto constants",
)
whitepoint_start = "float WhitePoint()\n{\n"
helpers = r'''float ExposureTrimAnchorKey(uint index)
{
    if (index == 0) return gExposureTrimAnchorExposure0;
    if (index == 1) return gExposureTrimAnchorExposure1;
    if (index == 2) return gExposureTrimAnchorExposure2;
    if (index == 3) return gExposureTrimAnchorExposure3;
    if (index == 4) return gExposureTrimAnchorExposure4;
    if (index == 5) return gExposureTrimAnchorExposure5;
    if (index == 6) return gExposureTrimAnchorExposure6;
    return gExposureTrimAnchorExposure7;
}

float ExposureTrimAnchorValue(uint index)
{
    if (index == 0) return gExposureTrimAnchorTrim0;
    if (index == 1) return gExposureTrimAnchorTrim1;
    if (index == 2) return gExposureTrimAnchorTrim2;
    if (index == 3) return gExposureTrimAnchorTrim3;
    if (index == 4) return gExposureTrimAnchorTrim4;
    if (index == 5) return gExposureTrimAnchorTrim5;
    if (index == 6) return gExposureTrimAnchorTrim6;
    return gExposureTrimAnchorTrim7;
}

float EffectiveExposureTrim(float key)
{
    const float fallback = clamp(gExposureTrim, 0.25, 50.0);
    const uint count = min(gExposureTrimAnchorCount, 8u);
    if (gExposureTrimPreview != 0 || count == 0 || !isfinite(key) || key <= 1e-8)
        return fallback;
    if (count == 1)
        return clamp(ExposureTrimAnchorValue(0), 0.25, 50.0);
    const float firstKey = ExposureTrimAnchorKey(0);
    const float lastKey = ExposureTrimAnchorKey(count - 1);
    if (key <= firstKey)
        return clamp(ExposureTrimAnchorValue(0), 0.25, 50.0);
    if (key >= lastKey)
        return clamp(ExposureTrimAnchorValue(count - 1), 0.25, 50.0);
    [unroll] for (uint i = 0; i < 7u; ++i)
    {
        if (i + 1 >= count)
            break;
        const float aKey = ExposureTrimAnchorKey(i);
        const float bKey = ExposureTrimAnchorKey(i + 1);
        if (key >= aKey && key <= bKey && bKey > aKey * 1.000001)
        {
            const float aTrim = max(ExposureTrimAnchorValue(i), 0.25);
            const float bTrim = max(ExposureTrimAnchorValue(i + 1), 0.25);
            const float t = (log(key) - log(aKey)) / (log(bKey) - log(aKey));
            return clamp(exp(lerp(log(aTrim), log(bTrim), t)), 0.25, 50.0);
        }
    }
    return clamp(ExposureTrimAnchorValue(count - 1), 0.25, 50.0);
}

float ExposureSample()
{
#ifdef VK_MODE
    return gMotion.Load(int3(0, 0, 0)).r;
#else
    return gExposure.Load(int3(0, 0, 0)).r;
#endif
}

'''
s = rep(s, whitepoint_start, helpers + whitepoint_start, "HLSL trim helpers")
old_wp = r'''float WhitePoint()
{
#ifndef VK_MODE
    if (gUseGameExposure != 0)
    {
        float e = gExposure.Load(int3(0, 0, 0)).r;
        if (e > 1e-6 && e < 1e6)
            return clamp(gExposurePreMul / e, 0.01, 4096.0);
        // A missing or absurd sample falls through to the CPU value the meter path still maintains.
    }
#endif
    return max(gWhitePoint, 1e-4);
}
'''
new_wp = r'''float WhitePoint()
{
    if (gUseGameExposure != 0 || gUseExposureWhitePoint != 0)
    {
        const float e = ExposureSample();
        if (isfinite(e) && e > 1e-8 && e < 1e8)
        {
            const float preExposure =
                (isfinite(gPreExposure) && gPreExposure > 1e-6) ? gPreExposure : 1.0;
            const float baseWhitePoint = preExposure / e;
            const float trim = EffectiveExposureTrim(baseWhitePoint);
            return clamp(baseWhitePoint * trim, 0.01, 4096.0);
        }
    }
    return max(gWhitePoint, 1e-4);
}
'''
s = rep(s, old_wp, new_wp, "HLSL live exposure white point")
old_meter = r'''    if (gMode == 3)
    {
        // Tile (0,0) carries the game's own exposure rather than a tile mean.
        //
        // The exposure is a 1x1 texture the game owns, in a resource state this pass did not set and
        // must not assume. Copying it would mean transitioning someone else's resource on a guess,
        // which is how a device is lost. Reading it as an SRV in a pass that is already running costs
        // nothing and touches no state -- and it rides back on the readback that already exists.
        //
        // The motion slot is free here: the meter has no use for motion vectors.
        if (id.x == 0 && id.y == 0)
        {
            gTarget[id.xy] = float4(gMotion.Load(int3(0, 0, 0)).r, 0.0, 0.0, 1.0);
            return;
        }

        uint fullW, fullH;
        gSource.GetDimensions(fullW, fullH);

        const uint tx0 = (uint) (((float) id.x * (float) fullW) / (float) gWidth);
        const uint tx1 = (uint) (((float) (id.x + 1) * (float) fullW) / (float) gWidth);
        const uint ty0 = (uint) (((float) id.y * (float) fullH) / (float) gHeight);
        const uint ty1 = (uint) (((float) (id.y + 1) * (float) fullH) / (float) gHeight);

        // A tile of a 4K frame is 60x34 pixels. Sampling a bounded number of them is within a percent
        // of the true mean and keeps the pass flat regardless of resolution.
        const uint stepX = max((tx1 - tx0) / 8u, 1u);
        const uint stepY = max((ty1 - ty0) / 8u, 1u);

        float sum = 0.0;
        uint taken = 0;

        for (uint ty = ty0; ty < max(ty1, ty0 + 1u); ty += stepY)
        {
            for (uint tx = tx0; tx < max(tx1, tx0 + 1u); tx += stepX)
            {
                float3 c = max(gSource.Load(int3(min(tx, fullW - 1u), min(ty, fullH - 1u), 0)).rgb, 0.0);
                sum += dot(c, kLuma);
                taken++;
            }
        }

        gTarget[id.xy] = float4(taken > 0u ? sum / (float) taken : 0.0, 0.0, 0.0, 1.0);
        return;
    }

'''
new_meter = r'''    if (gMode == 3)
    {
        if (gMeterCopiesExposure != 0)
        {
            if (id.x == 0 && id.y == 0)
                gTarget[id.xy] = float4(gMotion.Load(int3(0, 0, 0)).r, 0.0, 0.0, 1.0);
            return;
        }

        uint fullW, fullH;
        gSource.GetDimensions(fullW, fullH);
        const uint tx0 = (uint) (((float) id.x * (float) fullW) / (float) gWidth);
        const uint tx1 = (uint) (((float) (id.x + 1) * (float) fullW) / (float) gWidth);
        const uint ty0 = (uint) (((float) id.y * (float) fullH) / (float) gHeight);
        const uint ty1 = (uint) (((float) (id.y + 1) * (float) fullH) / (float) gHeight);

        float sum = 0.0;
        uint taken = 0;
        [loop] for (uint ty = ty0; ty < max(ty1, ty0 + 1u); ++ty)
        {
            [loop] for (uint tx = tx0; tx < max(tx1, tx0 + 1u); ++tx)
            {
                const float3 c = max(gSource.Load(int3(min(tx, fullW - 1u), min(ty, fullH - 1u), 0)).rgb, 0.0);
                const float luma = dot(c, kLuma);
                sum += isfinite(luma) ? max(luma, 0.0) : 0.0;
                taken++;
            }
        }
        gTarget[id.xy] = float4(taken > 0u ? sum / (float) taken : 0.0, 0.0, 0.0, 1.0);
        return;
    }

'''
s = rep(s, old_meter, new_meter, "HLSL exact meter")
auto_mode = r'''    // OptiScaler automatic exposure. The 64x64 input contains exact tile means from the
    // original linear-HDR frame. Every tile contributes by its real pixel area.
    if (gMode == 11)
    {
        if (id.x != 0 || id.y != 0)
            return;

        const uint srcW = max(gExposureSourceWidth, 1u);
        const uint srcH = max(gExposureSourceHeight, 1u);
        const float preExposure =
            (isfinite(gPreExposure) && gPreExposure > 1e-6) ? gPreExposure : 1.0;
        const float protection = saturate(gAutoExposureShadowProtection * 0.01);

        float weightedBufferLuma = 0.0;
        float weightedSceneLogLuma = 0.0;
        float totalPixels = 0.0;

        [loop] for (uint ty = 0u; ty < 64u; ++ty)
        {
            const uint y0 = (ty * srcH) / 64u;
            const uint y1 = ((ty + 1u) * srcH) / 64u;
            const uint tileH = max(y1 - y0, 1u);
            [loop] for (uint tx = 0u; tx < 64u; ++tx)
            {
                const uint x0 = (tx * srcW) / 64u;
                const uint x1 = ((tx + 1u) * srcW) / 64u;
                const uint tileW = max(x1 - x0, 1u);
                const float pixels = (float) tileW * (float) tileH;
                const float tileMean = max(SanitizeFinite(gSource.Load(int3(tx, ty, 0)).r, 0.0), 0.0);
                weightedBufferLuma += tileMean * pixels;
                totalPixels += pixels;
                if (protection > 0.0)
                {
                    const float sceneLuma = max(tileMean / preExposure, 1e-8);
                    weightedSceneLogLuma += clamp(log2(sceneLuma), -24.0, 24.0) * pixels;
                }
            }
        }

        const float averageBufferLuma = totalPixels > 0.0 ? weightedBufferLuma / totalPixels : 0.0;
        float meteredSceneLuma = averageBufferLuma / preExposure;

        if (protection > 0.0 && totalPixels > 0.0)
        {
            const float referenceLogLuma = weightedSceneLogLuma / totalPixels;
            const float highlightKneeEv = lerp(3.0, 1.0, protection);
            const float highlightCompressionSlope = lerp(1.0, 0.35, protection);
            float protectedLinearSum = 0.0;

            [loop] for (uint ty2 = 0u; ty2 < 64u; ++ty2)
            {
                const uint y0 = (ty2 * srcH) / 64u;
                const uint y1 = ((ty2 + 1u) * srcH) / 64u;
                const uint tileH = max(y1 - y0, 1u);
                [loop] for (uint tx2 = 0u; tx2 < 64u; ++tx2)
                {
                    const uint x0 = (tx2 * srcW) / 64u;
                    const uint x1 = ((tx2 + 1u) * srcW) / 64u;
                    const uint tileW = max(x1 - x0, 1u);
                    const float pixels = (float) tileW * (float) tileH;
                    const float tileMean = max(SanitizeFinite(gSource.Load(int3(tx2, ty2, 0)).r, 0.0), 0.0);
                    const float sceneLuma = max(tileMean / preExposure, 1e-8);
                    const float logLuma = clamp(log2(sceneLuma), -24.0, 24.0);
                    const float deltaEv = logLuma - referenceLogLuma;
                    float compressedLogLuma = logLuma;
                    if (deltaEv > highlightKneeEv)
                        compressedLogLuma = referenceLogLuma + highlightKneeEv +
                                            (deltaEv - highlightKneeEv) * highlightCompressionSlope;
                    protectedLinearSum += exp2(clamp(compressedLogLuma, -24.0, 24.0)) * pixels;
                }
            }
            const float protectedAverage = protectedLinearSum / totalPixels;
            if (isfinite(protectedAverage) && protectedAverage > 1e-8)
                meteredSceneLuma = protectedAverage;
        }

        float exposure = meteredSceneLuma > 1e-8 ? 0.18 / (meteredSceneLuma * 0.82) : 1.0;
        if (!isfinite(exposure) || exposure <= 0.0)
            exposure = 1.0;
        gTarget[uint2(0, 0)] = float4(exposure, 0.0, 0.0, 1.0);
        return;
    }

'''
s = rep(s, "    if (gMode == 4)\n", auto_mode + "    if (gMode == 4)\n", "HLSL auto mode insertion")
write(rel, s)

# -----------------------------------------------------------------------------
# Vulkan state, model resource and same-frame automatic exposure.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNrFeature_Vk_Internal.h"
s = read(rel)
s = rep(
    s,
    "    OwnedImage meter;\n",
    "    OwnedImage meter;\n"
    "    OwnedImage autoExposure;\n"
    "    bool autoExposureActive = false;\n"
    "    float autoExposureValue = 0.0f;\n"
    "    float autoExposurePreExposure = 1.0f;\n"
    "    unsigned long long autoExposureFrames = 0;\n"
    "    uint32_t exposureReadbackSource = 0;\n"
    "    uint32_t meterExposureKind[4] = {};\n"
    "    float meterExposurePreExposure[4] = {};\n",
    "Vulkan auto exposure state",
)
s = rep(s, "constexpr uint32_t kMeterSide = 8;\n", "constexpr uint32_t kMeterSide = 64;\n", "Vulkan meter side")
s = rep(
    s,
    "                                  NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* output,\n",
    "                                  NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* exposure,\n"
    "                                  NVSDK_NGX_Resource_VK* output,\n",
    "Vulkan EvaluateModel exposure declaration",
)
write(rel, s)

rel = "OptiScaler/dlssnr/DlssNrFeature_Vk_Model.cpp"
s = read(rel)
s = rep(
    s,
    "                              NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* output,\n",
    "                              NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* exposure,\n"
    "                              NVSDK_NGX_Resource_VK* output,\n",
    "Vulkan EvaluateModel exposure definition",
)
s = rep(
    s,
    '    parameters->Set("DLSSNR.MVec", static_cast<void*>(motion));\n'
    '    parameters->Set("DLSSNR.Output", static_cast<void*>(output));\n',
    '    parameters->Set("DLSSNR.MVec", static_cast<void*>(motion));\n'
    '    parameters->Set("DLSSNR.ExposureTexture", static_cast<void*>(exposure));\n'
    '    parameters->Set("DLSSNR.Output", static_cast<void*>(output));\n',
    "Vulkan model exposure parameter",
)
s = rep(
    s,
    "    DestroyImage(state.meter);\n"
    "    DestroyMeterReadback();\n",
    "    DestroyImage(state.meter);\n"
    "    DestroyImage(state.autoExposure);\n"
    "    DestroyMeterReadback();\n",
    "Vulkan auto exposure shutdown",
)
write(rel, s)

rel = "OptiScaler/dlssnr/DlssNrFeature_Vk_Resources.cpp"
s = read(rel)
# no structural changes required here beyond the larger kMeterSide constant used automatically
write(rel, s)

rel = "OptiScaler/dlssnr/DlssNrFeature_Vk.cpp"
s = read(rel)
vulkan_helpers = r'''namespace
{
struct VkTrimAnchor
{
    float key = 0.0f;
    float trim = 1.0f;
};

std::vector<VkTrimAnchor> ParseVkTrimAnchors(const std::string& text)
{
    std::vector<VkTrimAnchor> out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token = text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;
        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;
        try
        {
            const float key = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));
            if (std::isfinite(key) && key > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ key, std::clamp(trim, 0.25f, 50.0f) });
        }
        catch (...) {}
    }
    std::sort(out.begin(), out.end(), [](const VkTrimAnchor& a, const VkTrimAnchor& b) { return a.key < b.key; });
    return out;
}

float VkTrimForKey(float key, float fallback, const std::vector<VkTrimAnchor>& anchors, bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 50.0f);
    if (preview || anchors.empty() || !(std::isfinite(key) && key > 1e-8f))
        return fallback;
    if (anchors.size() == 1)
        return anchors[0].trim;
    if (key <= anchors.front().key)
        return anchors.front().trim;
    if (key >= anchors.back().key)
        return anchors.back().trim;
    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];
        if (key >= a.key && key <= b.key && b.key > a.key * 1.000001f)
        {
            const float t = (std::log(key) - std::log(a.key)) / (std::log(b.key) - std::log(a.key));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 50.0f);
        }
    }
    return anchors.back().trim;
}

void FillVkTrimConstants(DlssNrConstants& params, const Config& cfg, uint32_t source)
{
    const bool automatic = source == 3;
    const float fallback = automatic ? cfg.DlssNrAutoExposureTrim.value_or_default()
                                     : cfg.DlssNrWhitePointTrim.value_or_default();
    const bool preview = automatic ? cfg.DlssNrAutoExposureTrimPreview.value_or_default()
                                   : cfg.DlssNrGameExposureTrimPreview.value_or_default();
    const auto anchors = ParseVkTrimAnchors(automatic ? cfg.DlssNrAutoExposureTrimAnchors.value_or_default()
                                                      : cfg.DlssNrGameExposureTrimAnchors.value_or_default());
    params.ExposureTrim = std::clamp(fallback, 0.25f, 50.0f);
    params.ExposureTrimPreview = preview ? 1u : 0u;
    params.ExposureTrimAnchorCount = (uint32_t) std::min<size_t>(anchors.size(), 8);
    float* pairs = &params.ExposureTrimAnchorExposure0;
    for (size_t i = 0; i < params.ExposureTrimAnchorCount; ++i)
    {
        pairs[i * 2 + 0] = anchors[i].key;
        pairs[i * 2 + 1] = anchors[i].trim;
    }
    params.AutoExposureShadowProtection =
        std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);
}
}

'''
s = rep(s, "namespace DlssNr\n{\n", "namespace DlssNr\n{\n" + vulkan_helpers, "Vulkan trim helpers")
s = rep(
    s,
    "            exposure.preExposure = state.gamePreExposure;\n"
    "            PublishStatus(owner, Backend::Vulkan,\n"
    "                          { state.models[0].feature != nullptr && !state.failed, state.reason, state.lastGpuTime,\n"
    "                            state.frames, exposure, false });\n",
    "            exposure.preExposure = state.gamePreExposure;\n"
    "            ExposureStatus automatic {};\n"
    "            automatic.seenFrames = state.autoExposureFrames;\n"
    "            automatic.offeredNow = state.autoExposureActive;\n"
    "            automatic.everOffered = state.autoExposureFrames != 0;\n"
    "            automatic.exposure = state.autoExposureValue;\n"
    "            automatic.preExposure = state.autoExposurePreExposure;\n"
    "            PublishStatus(owner, Backend::Vulkan,\n"
    "                          { state.models[0].feature != nullptr && !state.failed, state.reason, state.lastGpuTime,\n"
    "                            state.frames, exposure, automatic, false });\n",
    "Vulkan publish auto exposure",
)
# Change delayed readback consumption to respect which source produced the slot.
s = rep(
    s,
    "            if (std::isfinite(measured) && measured > 0.0f)\n"
    "                state.gameExposure = measured;\n",
    "            const auto slot = state.meterFrames % kMeterSlots;\n"
    "            if (std::isfinite(measured) && measured > 0.0f)\n"
    "            {\n"
    "                if (state.meterExposureKind[slot] == 1u)\n"
    "                    state.gameExposure = measured;\n"
    "                else if (state.meterExposureKind[slot] == 2u)\n"
    "                {\n"
    "                    state.autoExposureValue = measured;\n"
    "                    state.autoExposurePreExposure = state.meterExposurePreExposure[slot];\n"
    "                }\n"
    "            }\n",
    "Vulkan consume exposure kind",
)
# Allocation: meter + 1x1 automatic exposure.
s = rep(
    s,
    "        const bool meterReady =\n"
    "            (state.meter.Valid() || CreateImage(state.meter, kMeterSide, kMeterSide, VK_FORMAT_R32_SFLOAT, true)) &&\n"
    "            CreateMeterReadback();\n\n"
    "        if (!meterReady)\n"
    "            LOG_WARN(\"DLSS-NR Vulkan: no exposure meter; the white point stays on the slider\");\n",
    "        const bool meterReady =\n"
    "            (state.meter.Valid() || CreateImage(state.meter, kMeterSide, kMeterSide, VK_FORMAT_R32_SFLOAT, true)) &&\n"
    "            (state.autoExposure.Valid() || CreateImage(state.autoExposure, 1, 1, VK_FORMAT_R32_SFLOAT, true)) &&\n"
    "            CreateMeterReadback();\n\n"
    "        if (!meterReady)\n"
    "            LOG_WARN(\"DLSS-NR Vulkan: no exposure meter; automatic exposure is unavailable\");\n",
    "Vulkan auto exposure allocation",
)
# White point CPU fallback / anchors.
s = rep(
    s,
    "    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && state.gameExposure > 1e-6f)\n"
    "    {\n"
    "        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);\n"
    "        whitePoint = std::clamp(state.gamePreExposure / state.gameExposure * trim, 0.01f, 4096.0f);\n"
    "    }\n",
    "    const uint32_t requestedWhitePointSource = cfg.DlssNrWhitePointSource.value_or_default();\n"
    "    if (requestedWhitePointSource != state.exposureReadbackSource)\n"
    "    {\n"
    "        state.exposureReadbackSource = requestedWhitePointSource;\n"
    "        state.meterFrames = 0;\n"
    "        state.gameExposure = 0.0f;\n"
    "        state.autoExposureValue = 0.0f;\n"
    "        for (auto& kind : state.meterExposureKind) kind = 0u;\n"
    "    }\n"
    "    if (requestedWhitePointSource == 1 && state.gameExposure > 1e-6f)\n"
    "    {\n"
    "        const float baseWhitePoint = state.gamePreExposure / state.gameExposure;\n"
    "        const auto anchors = ParseVkTrimAnchors(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float trim = VkTrimForKey(baseWhitePoint, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "                                       cfg.DlssNrGameExposureTrimPreview.value_or_default());\n"
    "        whitePoint = std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n"
    "    }\n"
    "    else if (requestedWhitePointSource == 3 && state.autoExposureValue > 1e-6f)\n"
    "    {\n"
    "        const float baseWhitePoint = state.autoExposurePreExposure / state.autoExposureValue;\n"
    "        const auto anchors = ParseVkTrimAnchors(cfg.DlssNrAutoExposureTrimAnchors.value_or_default());\n"
    "        const float trim = VkTrimForKey(baseWhitePoint, cfg.DlssNrAutoExposureTrim.value_or_default(), anchors,\n"
    "                                       cfg.DlssNrAutoExposureTrimPreview.value_or_default());\n"
    "        whitePoint = std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n"
    "    }\n",
    "Vulkan CPU exposure anchors",
)
# Add automatic meter/reduce before encode.
vk_auto_marker = "    auto encode = DlssNr_Common::MakeConstants(DlssNrMode_Encode, width, height, whitePoint, linearHdr, cfg);\n"
vk_auto_code = r'''    state.autoExposureActive = false;
    if (requestedWhitePointSource == 3 && linearHdr && state.meter.Valid() && state.autoExposure.Valid())
    {
        DlssNrConstants meter {};
        meter.Mode = DlssNrMode_Meter;
        meter.Width = kMeterSide;
        meter.Height = kMeterSide;
        meter.MeterCopiesExposure = 0;
        Transition(cmdBuffer, state.meter, VK_IMAGE_LAYOUT_GENERAL);
        if (state.pass->Dispatch(cmdBuffer, meter, kMeterSide, kMeterSide,
                                 colour->Resource.ImageViewInfo.ImageView, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                 VK_NULL_HANDLE, state.meter.view, VK_NULL_HANDLE, inputLayout))
        {
            Transition(cmdBuffer, state.meter, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, state.autoExposure, VK_IMAGE_LAYOUT_GENERAL);

            DlssNrConstants reduce {};
            reduce.Mode = DlssNrMode_AutoExposure;
            reduce.Width = 1;
            reduce.Height = 1;
            reduce.PreExposure = frame.PreExposure;
            reduce.ExposureSourceWidth = width;
            reduce.ExposureSourceHeight = height;
            reduce.AutoExposureShadowProtection =
                std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);

            if (state.pass->Dispatch(cmdBuffer, reduce, 1, 1, state.meter.view, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, state.autoExposure.view, VK_NULL_HANDLE,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
            {
                Transition(cmdBuffer, state.autoExposure, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                state.autoExposureActive = true;

                const unsigned long long slot = state.meterFrames % kMeterSlots;
                if (state.meterReadback[slot] != VK_NULL_HANDLE)
                {
                    state.meterExposureKind[slot] = 2u;
                    state.meterExposurePreExposure[slot] =
                        std::isfinite(frame.PreExposure) && frame.PreExposure > 1e-6f ? frame.PreExposure : 1.0f;
                    Transition(cmdBuffer, state.autoExposure, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    VkBufferImageCopy region {};
                    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    region.imageExtent = { 1, 1, 1 };
                    vkCmdCopyImageToBuffer(cmdBuffer, state.autoExposure.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           state.meterReadback[slot], 1, &region);
                    VkBufferMemoryBarrier toHost {};
                    toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toHost.buffer = state.meterReadback[slot];
                    toHost.size = sizeof(float);
                    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                                         0, nullptr, 1, &toHost, 0, nullptr);
                    Transition(cmdBuffer, state.autoExposure, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    state.meterFrames++;
                    state.autoExposureFrames++;
                }
            }
        }
    }

'''
s = rep(s, vk_auto_marker, vk_auto_code + vk_auto_marker, "Vulkan automatic exposure dispatch")
# Populate encode constants and bind automatic exposure through the otherwise-unused motion slot.
s = rep(
    s,
    "    encode.GuideWidth = guideWidth;\n"
    "    encode.GuideHeight = guideHeight;\n",
    "    encode.GuideWidth = guideWidth;\n"
    "    encode.GuideHeight = guideHeight;\n"
    "    encode.PreExposure = frame.PreExposure;\n"
    "    encode.UseExposureWhitePoint = state.autoExposureActive ? 1u : 0u;\n"
    "    FillVkTrimConstants(encode, cfg, requestedWhitePointSource);\n",
    "Vulkan encode auto constants",
)
s = rep(
    s,
    "                              VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, state.proxy.view, state.keep.view,\n"
    "                              inputLayout))\n",
    "                              VK_NULL_HANDLE, VK_NULL_HANDLE,\n"
    "                              state.autoExposureActive ? state.autoExposure.view : VK_NULL_HANDLE,\n"
    "                              state.proxy.view, state.keep.view, inputLayout,\n"
    "                              state.autoExposureActive ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL\n"
    "                                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
    "Vulkan encode exposure binding",
)
# Game meter flag and kind.
s = rep(
    s,
    "            meter.Mode = DlssNrMode_Meter;\n"
    "            meter.Width = kMeterSide;\n"
    "            meter.Height = kMeterSide;\n",
    "            meter.Mode = DlssNrMode_Meter;\n"
    "            meter.Width = kMeterSide;\n"
    "            meter.Height = kMeterSide;\n"
    "            meter.MeterCopiesExposure = 1;\n",
    "Vulkan game meter flag",
)
s = rep(
    s,
    "                state.meterFrames++;\n",
    "                state.meterExposureKind[slot] = 1u;\n"
    "                state.meterExposurePreExposure[slot] = state.gamePreExposure;\n"
    "                state.meterFrames++;\n",
    "Vulkan game meter kind",
)
# Model gets only the owned automatic exposure texture.
s = rep(
    s,
    "        evaluated = EvaluateModel(cmdBuffer, pass, &input->ngx, depth, motion, &answer->ngx,\n",
    "        NVSDK_NGX_Resource_VK* modelExposure = state.autoExposureActive ? &state.autoExposure.ngx : nullptr;\n"
    "        evaluated = EvaluateModel(cmdBuffer, pass, &input->ngx, depth, motion, modelExposure, &answer->ngx,\n",
    "Vulkan model auto exposure",
)
# Resolve uses same-frame automatic exposure through gMotion/t3.
s = rep(
    s,
    "    resolve.Mode = DlssNrMode_Resolve;\n",
    "    resolve.Mode = DlssNrMode_Resolve;\n"
    "    resolve.PreExposure = frame.PreExposure;\n"
    "    resolve.UseExposureWhitePoint = state.autoExposureActive ? 1u : 0u;\n"
    "    FillVkTrimConstants(resolve, cfg, requestedWhitePointSource);\n",
    "Vulkan resolve auto constants",
)
s = rep(
    s,
    "                              state.keep.view, VK_NULL_HANDLE, target.ImageView, VK_NULL_HANDLE,\n"
    "                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
    "                              state.keep.view,\n"
    "                              state.autoExposureActive ? state.autoExposure.view : VK_NULL_HANDLE,\n"
    "                              target.ImageView, VK_NULL_HANDLE,\n"
    "                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,\n"
    "                              state.autoExposureActive ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL\n"
    "                                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
    "Vulkan resolve exposure binding",
)
write(rel, s)

print("OptiScaler v0.8.4 Automatic Exposure port applied")
