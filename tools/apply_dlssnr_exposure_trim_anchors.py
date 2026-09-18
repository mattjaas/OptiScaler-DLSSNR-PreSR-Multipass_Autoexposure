from pathlib import Path

HERE = Path(__file__).resolve()
_candidates = [Path.cwd(), HERE.parent]
if len(HERE.parents) >= 3:
    _candidates.append(HERE.parents[2])
ROOT = next((p for p in _candidates if (p / "OptiScaler").is_dir()), None)
if ROOT is None:
    raise RuntimeError("Run this script from the repository root (the directory containing OptiScaler).")


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


def write(rel, text):
    (ROOT / rel).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# This patch is intentionally applied after apply_dlssnr_autoexposure.py.
# It adds exposure-dependent Trim calibration for the two real exposure sources
# without changing the checked-in OptiScaler sources outside the release runner.

# -----------------------------------------------------------------------------
# Persistent anchor tables + non-persistent preview switches.
# -----------------------------------------------------------------------------
rel = "OptiScaler/Config.h"
s = read(rel)
s = replace_once(
    s,
    "    // Separate trim for OptiScaler's own GPU-calculated exposure (WhitePointSource == 3).\n"
    "    CustomOptional<float> DlssNrAutoExposureTrim { 1.0f };\n\n"
    "    CustomOptional<bool> DlssNrScanMeter { false };\n",
    "    // Separate trim for OptiScaler's own GPU-calculated exposure (WhitePointSource == 3).\n"
    "    CustomOptional<float> DlssNrAutoExposureTrim { 1.0f };\n\n"
    "    // Exposure-dependent Trim calibration tables, serialized as exposure:trim pairs.\n"
    "    // The two sources are intentionally separate because their exposure scales need not match.\n"
    "    CustomOptional<std::string> DlssNrGameExposureTrimAnchors { std::string() };\n"
    "    CustomOptional<std::string> DlssNrAutoExposureTrimAnchors { std::string() };\n\n"
    "    // Calibration aid only. These are deliberately not persisted: after a restart anchors are\n"
    "    // active again, rather than a forgotten preview checkbox silently bypassing them.\n"
    "    CustomOptional<bool> DlssNrGameExposureTrimPreview { false };\n"
    "    CustomOptional<bool> DlssNrAutoExposureTrimPreview { false };\n\n"
    "    CustomOptional<bool> DlssNrScanMeter { false };\n",
    "trim anchor config fields",
)
write(rel, s)

rel = "OptiScaler/Config.cpp"
s = read(rel)
s = replace_once(
    s,
    "            DlssNrWhitePointSource.set_from_config(readUInt(\"DlssNr\", \"WhitePointSource\"));\n"
    "            DlssNrAutoExposureTrim.set_from_config(readFloat(\"DlssNr\", \"AutoExposureTrim\"));\n",
    "            DlssNrWhitePointSource.set_from_config(readUInt(\"DlssNr\", \"WhitePointSource\"));\n"
    "            DlssNrAutoExposureTrim.set_from_config(readFloat(\"DlssNr\", \"AutoExposureTrim\"));\n"
    "            DlssNrGameExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"GameExposureTrimAnchors\"));\n"
    "            DlssNrAutoExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"AutoExposureTrimAnchors\"));\n",
    "read trim anchors",
)
s = replace_once(
    s,
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrim\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"WhitePointTrim\", GetFloatValue(Instance()->DlssNrWhitePointTrim.value_for_config()).c_str());\n",
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrim\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"GameExposureTrimAnchors\",\n"
    "                 Instance()->DlssNrGameExposureTrimAnchors.value_for_config_or(\"\").c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrimAnchors\",\n"
    "                 Instance()->DlssNrAutoExposureTrimAnchors.value_for_config_or(\"\").c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"WhitePointTrim\", GetFloatValue(Instance()->DlssNrWhitePointTrim.value_for_config()).c_str());\n",
    "write trim anchors",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Public status for the generated 1x1 automatic exposure value.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNrFeature_Dx12.h"
s = read(rel)
s = replace_once(
    s,
    "ExposureStatus GameExposureStatus();\n",
    "ExposureStatus GameExposureStatus();\n"
    "ExposureStatus AutoExposureStatus();\n",
    "auto exposure status declaration",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Menu helpers + controls.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Menu.cpp"
s = read(rel)
s = replace_once(
    s,
    "#include <string>\n#include <unordered_map>\n",
    "#include <string>\n#include <vector>\n#include <unordered_map>\n",
    "menu vector include",
)
helper_marker = "// A slider that only writes its value when the handle is released.\n"
helper_code = r'''struct ExposureTrimAnchorUi
{
    float exposure = 0.0f;
    float trim = 1.0f;
};

static std::vector<ExposureTrimAnchorUi> ParseExposureTrimAnchorsUi(const std::string& text)
{
    std::vector<ExposureTrimAnchorUi> out;
    size_t pos = 0;

    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token =
            text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;

        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;

        try
        {
            const float exposure = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));

            if (std::isfinite(exposure) && exposure > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ exposure, std::clamp(trim, 0.25f, 10.0f) });
        }
        catch (...)
        {
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b)
              { return a.exposure < b.exposure; });
    return out;
}

static std::string SerializeExposureTrimAnchorsUi(const std::vector<ExposureTrimAnchorUi>& anchors)
{
    std::string out;
    char buf[64];

    for (const auto& p : anchors)
    {
        snprintf(buf, sizeof(buf), "%.7g:%.7g;", p.exposure, p.trim);
        out += buf;
    }

    return out;
}

static bool UpsertExposureTrimAnchorUi(std::vector<ExposureTrimAnchorUi>& anchors, float exposure,
                                       float trim)
{
    if (!(std::isfinite(exposure) && exposure > 1e-8f))
        return false;

    trim = std::clamp(trim, 0.25f, 10.0f);

    for (auto& p : anchors)
    {
        if (exposure > p.exposure * 0.98f && exposure < p.exposure * 1.02f)
        {
            p.exposure = exposure;
            p.trim = trim;
            std::sort(anchors.begin(), anchors.end(),
                      [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b)
                      { return a.exposure < b.exposure; });
            return true;
        }
    }

    if (anchors.size() >= 8)
        return false;

    anchors.push_back({ exposure, trim });
    std::sort(anchors.begin(), anchors.end(),
              [](const ExposureTrimAnchorUi& a, const ExposureTrimAnchorUi& b)
              { return a.exposure < b.exposure; });
    return true;
}

static float ExposureTrimForUi(float exposure, float fallback,
                               const std::vector<ExposureTrimAnchorUi>& anchors, bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 10.0f);

    if (preview || anchors.empty() || !(std::isfinite(exposure) && exposure > 1e-8f))
        return fallback;

    if (anchors.size() == 1)
        return anchors[0].trim;

    if (exposure <= anchors.front().exposure)
        return anchors.front().trim;
    if (exposure >= anchors.back().exposure)
        return anchors.back().trim;

    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];

        if (exposure >= a.exposure && exposure <= b.exposure &&
            b.exposure > a.exposure * 1.000001f)
        {
            const float t = (std::log(exposure) - std::log(a.exposure)) /
                            (std::log(b.exposure) - std::log(a.exposure));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 10.0f);
        }
    }

    return anchors.back().trim;
}

static void RenderExposureTrimAnchorControls(CustomOptional<std::string>& storedAnchors,
                                             CustomOptional<bool>& previewSetting, float exposure,
                                             float sliderTrim, const char* idSuffix)
{
    auto anchors = ParseExposureTrimAnchorsUi(storedAnchors.value_or_default());
    const bool haveExposure = std::isfinite(exposure) && exposure > 1e-8f;

    const std::string addLabel = std::string("Add Anchor point##") + idSuffix;
    ImGui::BeginDisabled(!haveExposure);
    if (ImGui::Button(addLabel.c_str()))
    {
        if (UpsertExposureTrimAnchorUi(anchors, exposure, sliderTrim))
            storedAnchors = SerializeExposureTrimAnchorsUi(anchors);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    bool preview = previewSetting.value_or_default();
    const std::string previewLabel =
        std::string("Preview Trim value for actual scene##") + idSuffix;
    if (ImGui::Checkbox(previewLabel.c_str(), &preview))
        previewSetting = preview;

    HelpMarker("When enabled, anchor points are temporarily ignored and the Trim slider is applied"
               "\ndirectly to the current scene. Use this to find the right Trim for the current"
               "\nexposure, then press Add Anchor point. The preview switch is not saved across restarts.");

    if (!haveExposure)
        ImGui::TextDisabled("Waiting for a valid exposure before an Anchor point can be added.");
    else
    {
        const float effective = ExposureTrimForUi(exposure, sliderTrim, anchors, preview);
        ImGui::TextDisabled("Current exposure %.5f -> effective Trim %.2fx%s", exposure, effective,
                            preview ? " (preview)" : "");
    }

    if (anchors.empty())
    {
        ImGui::TextDisabled("No Trim Anchor points: the Trim slider is used for every exposure.");
        return;
    }

    if (anchors.size() == 1)
        ImGui::TextDisabled("1 Trim Anchor point: its Trim is used for every exposure.");
    else
        ImGui::TextDisabled("%u Trim Anchor points: Trim is interpolated between exposure values.",
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
        ImGui::Text("Exposure %.5f  ->  Trim %.2fx", anchors[i].exposure, anchors[i].trim);
        ImGui::PopID();
    }
    ImGui::PopID();
}

'''
s = replace_once(s, helper_marker, helper_code + helper_marker, "menu trim anchor helpers")
s = replace_once(
    s,
    "            const auto ex = DlssNr::GameExposureStatus();\n"
    "            const bool vk = DlssNr::IsRunningVk();\n",
    "            const auto ex = DlssNr::GameExposureStatus();\n"
    "            const auto autoEx = DlssNr::AutoExposureStatus();\n"
    "            const bool vk = DlssNr::IsRunningVk();\n",
    "menu auto exposure status fetch",
)
s = replace_once(
    s,
    "                    const float trim =\n"
    "                        std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 10.0f);\n",
    "                    const auto trimAnchors =\n"
    "                        ParseExposureTrimAnchorsUi(config->DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "                    const float trim = ExposureTrimForUi(\n"
    "                        ex.exposure, config->DlssNrWhitePointTrim.value_or_default(), trimAnchors,\n"
    "                        config->DlssNrGameExposureTrimPreview.value_or_default());\n",
    "menu game exposure effective trim",
)
s = replace_once(
    s,
    r'''            else if (source == 3)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "GPU Automatic exposure is active; existing scan/game modes are unchanged.");
            }
''',
    r'''            else if (source == 3)
            {
                if (vk)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "Automatic exposure is currently available on D3D12 only.");
                else if (autoEx.exposure > 1e-8f)
                {
                    const auto trimAnchors =
                        ParseExposureTrimAnchorsUi(config->DlssNrAutoExposureTrimAnchors.value_or_default());
                    const float trim = ExposureTrimForUi(
                        autoEx.exposure, config->DlssNrAutoExposureTrim.value_or_default(), trimAnchors,
                        config->DlssNrAutoExposureTrimPreview.value_or_default());
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Automatic exposure %.4f  ->  white point %.2f", autoEx.exposure,
                                       autoEx.preExposure / autoEx.exposure * trim);
                    ImGui::TextDisabled("Calculated by OptiScaler; the game's ExposureTexture is ignored.");
                }
                else
                    ImGui::TextDisabled("Calculating automatic exposure...");
            }
''',
    "menu automatic exposure value",
)
old_trim_blocks = r'''        else if (wpSource == 1)
        {
            float gameTrim = config->DlssNrWhitePointTrim.value_or_default();

            if (ImGui::SliderFloat("Trim (x the game's exposure)", &gameTrim, 0.25f, 10.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointTrim = std::clamp(gameTrim, 0.25f, 10.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##wptrim"))
                config->DlssNrWhitePointTrim = 1.0f;

            HelpMarker("Multiplier on the white point derived from the game's own ExposureTexture."
                       "\n\n1.00x uses the game's value unchanged. Range: 0.25x to 10.00x."
                       "\n\nThis source never switches to OptiScaler automatic exposure. If the game"
                       "\ndoes not supply ExposureTexture, the status above reports that this source"
                       "\nis unavailable.");
        }
        else if (wpSource == 3)
        {
            float autoTrim = config->DlssNrAutoExposureTrim.value_or_default();

            if (ImGui::SliderFloat("Trim (x automatic exposure)", &autoTrim, 0.25f, 10.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrAutoExposureTrim = std::clamp(autoTrim, 0.25f, 10.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##autoexposuretrim"))
                config->DlssNrAutoExposureTrim = 1.0f;

            HelpMarker("OptiScaler calculates exposure itself from the ORIGINAL linear-HDR frame"
                       "\nbefore Neural Rendering changes it."
                       "\n\nThis source always uses OptiScaler's calculation: the game's ExposureTexture"
                       "\nis ignored even when present. One 1x1 exposure value is calculated on the GPU"
                       "\nand reused by Encode, every Neural Rendering pass, and Resolve."
                       "\n\n1.00x uses the calculated value unchanged. Range: 0.25x to 10.00x."
                       "\nAutomatic exposure is currently D3D12 only.");
        }
'''
new_trim_blocks = r'''        else if (wpSource == 1)
        {
            float gameTrim = config->DlssNrWhitePointTrim.value_or_default();

            if (ImGui::SliderFloat("Trim (x the game's exposure)", &gameTrim, 0.25f, 10.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointTrim = std::clamp(gameTrim, 0.25f, 10.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##wptrim"))
            {
                config->DlssNrWhitePointTrim = 1.0f;
                gameTrim = 1.0f;
            }

            HelpMarker("Base Trim for the game's ExposureTexture. With no Anchor points this value is"
                       "\nused for every exposure. With Anchor points it becomes the calibration slider"
                       "\nand the anchored Trim curve is used instead. Range: 0.25x to 10.00x.");

            const auto gameStatus = DlssNr::GameExposureStatus();
            RenderExposureTrimAnchorControls(config->DlssNrGameExposureTrimAnchors,
                                             config->DlssNrGameExposureTrimPreview,
                                             gameStatus.exposure, gameTrim, "gameExposureTrim");
        }
        else if (wpSource == 3)
        {
            float autoTrim = config->DlssNrAutoExposureTrim.value_or_default();

            if (ImGui::SliderFloat("Trim (x automatic exposure)", &autoTrim, 0.25f, 10.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrAutoExposureTrim = std::clamp(autoTrim, 0.25f, 10.0f);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##autoexposuretrim"))
            {
                config->DlssNrAutoExposureTrim = 1.0f;
                autoTrim = 1.0f;
            }

            HelpMarker("Base Trim for OptiScaler automatic exposure. With no Anchor points this value"
                       "\nis used for every exposure. With Anchor points the Trim curve is evaluated"
                       "\nagainst the current 1x1 automatic exposure in the shader, so scene changes do"
                       "\nnot wait for the CPU readback shown in the menu. Range: 0.25x to 10.00x.");

            const auto autoStatus = DlssNr::AutoExposureStatus();
            RenderExposureTrimAnchorControls(config->DlssNrAutoExposureTrimAnchors,
                                             config->DlssNrAutoExposureTrimPreview,
                                             autoStatus.exposure, autoTrim, "automaticExposureTrim");
        }
'''
s = replace_once(s, old_trim_blocks, new_trim_blocks, "menu exposure trim anchor controls")
write(rel, s)

# -----------------------------------------------------------------------------
# Runtime: read back automatic exposure for UI/anchor capture, evaluate game Trim
# anchors on CPU, and pass automatic anchors to the shader for same-frame evaluation.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
s = read(rel)
s = replace_once(
    s,
    "    ID3D12Resource* autoExposure = nullptr;\n"
    "    bool autoExposureReadable = false;\n",
    "    ID3D12Resource* autoExposure = nullptr;\n"
    "    bool autoExposureReadable = false;\n"
    "    float autoExposureValue = 0.0f;\n"
    "    float autoExposurePreExposure = 1.0f;\n"
    "    unsigned long long autoExposureFrames = 0;\n"
    "    uint32_t exposureReadbackSource = 0;\n",
    "auto exposure CPU state",
)
s = replace_once(
    s,
    "    bool meterExposureValid[4] = {};\n"
    "    unsigned int meterSlot = 0;\n",
    "    // 0 = none, 1 = game ExposureTexture, 2 = OptiScaler automatic exposure.\n"
    "    unsigned int meterExposureKind[4] = {};\n"
    "    float meterExposurePreExposure[4] = { 1.0f, 1.0f, 1.0f, 1.0f };\n"
    "    unsigned int meterSlot = 0;\n",
    "meter readback kind",
)
s = replace_once(
    s,
    "    g_nr.meterExposureValid[slot] = exposureBound;\n",
    "    g_nr.meterExposureKind[slot] = exposureBound ? 1u : 0u;\n",
    "game meter readback kind",
)
auto_copy_marker = "    g_nr.meterFrames++;\n}\n\n// Takes the game's exposure out of tile 0 of the grid recorded three frames ago.\n"
auto_copy_code = r'''    g_nr.meterFrames++;
}

void CopyAutoExposureToReadback(ID3D12GraphicsCommandList* cmdList, float preExposure)
{
    if (g_nr.autoExposure == nullptr)
        return;

    const unsigned int slot = (unsigned int) (g_nr.meterFrames % 4);
    ID3D12Resource* buffer = g_nr.meterReadback[slot];

    if (buffer == nullptr)
        return;

    g_nr.meterExposureKind[slot] = 2u;
    g_nr.meterExposurePreExposure[slot] =
        std::isfinite(preExposure) && preExposure > 1e-6f ? preExposure : 1.0f;

    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = g_nr.autoExposure;
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

    Barrier(cmdList, g_nr.autoExposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(cmdList, g_nr.autoExposure, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    g_nr.meterFrames++;
    g_nr.autoExposureFrames++;
}

// Takes the game's exposure out of tile 0 of the grid recorded three frames ago.
'''
s = replace_once(s, auto_copy_marker, auto_copy_code, "automatic exposure readback copy")
s = replace_once(
    s,
    "    if (g_nr.meterExposureValid[slot] && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "        g_nr.gameExposure = src[0];\n",
    "    if (g_nr.meterExposureKind[slot] == 1u && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "        g_nr.gameExposure = src[0];\n"
    "    else if (g_nr.meterExposureKind[slot] == 2u && std::isfinite(src[0]) && src[0] > 0.0f)\n"
    "    {\n"
    "        g_nr.autoExposureValue = src[0];\n"
    "        g_nr.autoExposurePreExposure = g_nr.meterExposurePreExposure[slot];\n"
    "    }\n",
    "consume both exposure readbacks",
)
s = replace_once(
    s,
    "    g_nr.gameExposure = 0.0f;\n\n"
    "    for (bool& valid : g_nr.meterExposureValid)\n"
    "        valid = false;\n",
    "    g_nr.gameExposure = 0.0f;\n"
    "    g_nr.autoExposureValue = 0.0f;\n"
    "    g_nr.autoExposurePreExposure = 1.0f;\n\n"
    "    for (unsigned int& kind : g_nr.meterExposureKind)\n"
    "        kind = 0u;\n",
    "invalidate both exposure readbacks",
)
trim_helpers_marker = "// Turns what the meter saw into the divisor the encode uses, or falls back to the slider.\n"
trim_helpers = r'''struct ExposureTrimAnchorRuntime
{
    float exposure = 0.0f;
    float trim = 1.0f;
};

std::vector<ExposureTrimAnchorRuntime> ParseExposureTrimAnchorsRuntime(const std::string& text)
{
    std::vector<ExposureTrimAnchorRuntime> out;
    size_t pos = 0;

    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token =
            text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;

        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;

        try
        {
            const float exposure = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));

            if (std::isfinite(exposure) && exposure > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ exposure, std::clamp(trim, 0.25f, 10.0f) });
        }
        catch (...)
        {
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ExposureTrimAnchorRuntime& a, const ExposureTrimAnchorRuntime& b)
              { return a.exposure < b.exposure; });
    return out;
}

float ExposureTrimForRuntime(float exposure, float fallback,
                             const std::vector<ExposureTrimAnchorRuntime>& anchors, bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 10.0f);

    if (preview || anchors.empty() || !(std::isfinite(exposure) && exposure > 1e-8f))
        return fallback;

    if (anchors.size() == 1)
        return anchors[0].trim;

    if (exposure <= anchors.front().exposure)
        return anchors.front().trim;
    if (exposure >= anchors.back().exposure)
        return anchors.back().trim;

    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];

        if (exposure >= a.exposure && exposure <= b.exposure &&
            b.exposure > a.exposure * 1.000001f)
        {
            const float t = (std::log(exposure) - std::log(a.exposure)) /
                            (std::log(b.exposure) - std::log(a.exposure));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 10.0f);
        }
    }

    return anchors.back().trim;
}

void FillAutoExposureTrimConstants(DlssNrConstants& params, const Config& cfg)
{
    params.ExposureTrim =
        std::clamp(cfg.DlssNrAutoExposureTrim.value_or_default(), 0.25f, 10.0f);
    params.ExposureTrimPreview = cfg.DlssNrAutoExposureTrimPreview.value_or_default() ? 1u : 0u;

    const auto anchors =
        ParseExposureTrimAnchorsRuntime(cfg.DlssNrAutoExposureTrimAnchors.value_or_default());
    params.ExposureTrimAnchorCount = (uint32_t) std::min<size_t>(anchors.size(), 8);

    // The sixteen float fields below are deliberately contiguous pairs in DlssNrConstants.
    float* pairs = &params.ExposureTrimAnchorExposure0;
    for (size_t i = 0; i < params.ExposureTrimAnchorCount; ++i)
    {
        pairs[i * 2 + 0] = anchors[i].exposure;
        pairs[i * 2 + 1] = anchors[i].trim;
    }
}

'''
s = replace_once(s, trim_helpers_marker, trim_helpers + trim_helpers_marker, "runtime trim anchor helpers")
s = replace_once(
    s,
    "        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 10.0f);\n\n"
    "        return std::clamp(g_nr.gamePreExposure / g_nr.gameExposure * trim, 0.01f, 4096.0f);\n",
    "        const auto anchors =\n"
    "            ParseExposureTrimAnchorsRuntime(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float trim = ExposureTrimForRuntime(\n"
    "            g_nr.gameExposure, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "            cfg.DlssNrGameExposureTrimPreview.value_or_default());\n\n"
    "        return std::clamp(g_nr.gamePreExposure / g_nr.gameExposure * trim, 0.01f, 4096.0f);\n",
    "game exposure anchor trim runtime",
)
s = replace_once(
    s,
    "    const bool exposureSettingOn = cfg.DlssNrWhitePointSource.value_or_default() == 1;\n",
    "    const uint32_t requestedExposureSource = cfg.DlssNrWhitePointSource.value_or_default();\n"
    "    if (requestedExposureSource != g_nr.exposureReadbackSource)\n"
    "    {\n"
    "        InvalidateExposureMeter();\n"
    "        g_nr.exposureReadbackSource = requestedExposureSource;\n"
    "    }\n\n"
    "    const bool exposureSettingOn = requestedExposureSource == 1;\n",
    "exposure readback source switch",
)
s = replace_once(
    s,
    "        g_nr.autoExposureReadable = true;\n"
    "        nrExposure = g_nr.autoExposure;\n"
    "        usingAutoExposure = true;\n",
    "        g_nr.autoExposureReadable = true;\n"
    "        nrExposure = g_nr.autoExposure;\n"
    "        usingAutoExposure = true;\n\n"
    "        // UI and Anchor-point capture use a delayed CPU readback. The shader below still uses\n"
    "        // this frame's 1x1 texture directly, so Trim interpolation itself has no readback lag.\n"
    "        CopyAutoExposureToReadback(cmdList, frame.PreExposure);\n"
    "        ConsumeMeterReadback();\n",
    "automatic exposure readback dispatch",
)
s = replace_once(
    s,
    "    encodeParams.ExposureTrim =\n"
    "        std::clamp(cfg.DlssNrAutoExposureTrim.value_or_default(), 0.25f, 10.0f);\n"
    "    encodeParams.UseExposureWhitePoint = usingAutoExposure ? 1u : 0u;\n",
    "    FillAutoExposureTrimConstants(encodeParams, cfg);\n"
    "    encodeParams.UseExposureWhitePoint = usingAutoExposure ? 1u : 0u;\n",
    "encode automatic trim anchors",
)
s = replace_once(
    s,
    "    resolveParams.ExposureTrim =\n"
    "        std::clamp(cfg.DlssNrAutoExposureTrim.value_or_default(), 0.25f, 10.0f);\n"
    "    resolveParams.UseExposureWhitePoint = usingAutoExposure ? 1u : 0u;\n",
    "    FillAutoExposureTrimConstants(resolveParams, cfg);\n"
    "    resolveParams.UseExposureWhitePoint = usingAutoExposure ? 1u : 0u;\n",
    "resolve automatic trim anchors",
)
s = replace_once(
    s,
    "ExposureStatus GameExposureStatus()\n"
    "{\n"
    "    ExposureStatus s {};\n"
    "    s.seenFrames = g_nr.exposureFrames;\n"
    "    s.offeredNow = g_nr.exposureOfferedNow;\n"
    "    s.everOffered = g_nr.exposureEverOffered;\n"
    "    s.exposure = g_nr.gameExposure;\n"
    "    s.preExposure = g_nr.gamePreExposure;\n"
    "    return s;\n"
    "}\n",
    "ExposureStatus GameExposureStatus()\n"
    "{\n"
    "    ExposureStatus s {};\n"
    "    s.seenFrames = g_nr.exposureFrames;\n"
    "    s.offeredNow = g_nr.exposureOfferedNow;\n"
    "    s.everOffered = g_nr.exposureEverOffered;\n"
    "    s.exposure = g_nr.gameExposure;\n"
    "    s.preExposure = g_nr.gamePreExposure;\n"
    "    return s;\n"
    "}\n\n"
    "ExposureStatus AutoExposureStatus()\n"
    "{\n"
    "    ExposureStatus s {};\n"
    "    s.seenFrames = g_nr.autoExposureFrames;\n"
    "    s.offeredNow = g_nr.autoExposureReadable;\n"
    "    s.everOffered = g_nr.autoExposureFrames != 0;\n"
    "    s.exposure = g_nr.autoExposureValue;\n"
    "    s.preExposure = g_nr.autoExposurePreExposure;\n"
    "    return s;\n"
    "}\n",
    "automatic exposure status implementation",
)
s = replace_once(
    s,
    "    g_nr.autoExposureReadable = false;\n\n"
    "    if (g_nr.calib != nullptr)\n",
    "    g_nr.autoExposureReadable = false;\n"
    "    g_nr.autoExposureValue = 0.0f;\n"
    "    g_nr.autoExposurePreExposure = 1.0f;\n"
    "    g_nr.autoExposureFrames = 0;\n"
    "    g_nr.exposureReadbackSource = 0;\n\n"
    "    if (g_nr.calib != nullptr)\n",
    "reset automatic exposure status",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Constant buffer + HLSL interpolation for same-frame automatic Trim anchors.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Common.h"
s = read(rel)
anchor_fields_cpp = """    uint32_t ExposureTrimAnchorCount;\n    uint32_t ExposureTrimPreview;\n    float ExposureTrimAnchorExposure0;\n    float ExposureTrimAnchorTrim0;\n    float ExposureTrimAnchorExposure1;\n    float ExposureTrimAnchorTrim1;\n    float ExposureTrimAnchorExposure2;\n    float ExposureTrimAnchorTrim2;\n    float ExposureTrimAnchorExposure3;\n    float ExposureTrimAnchorTrim3;\n    float ExposureTrimAnchorExposure4;\n    float ExposureTrimAnchorTrim4;\n    float ExposureTrimAnchorExposure5;\n    float ExposureTrimAnchorTrim5;\n    float ExposureTrimAnchorExposure6;\n    float ExposureTrimAnchorTrim6;\n    float ExposureTrimAnchorExposure7;\n    float ExposureTrimAnchorTrim7;\n"""
s = replace_once(
    s,
    "    float ExposureTrim;\n"
    "    uint32_t UseExposureWhitePoint;\n"
    "};\n",
    "    float ExposureTrim;\n"
    "    uint32_t UseExposureWhitePoint;\n" + anchor_fields_cpp + "};\n",
    "automatic trim anchor constants",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
s = read(rel)
anchor_fields_hlsl = """    uint  gExposureTrimAnchorCount;\n    uint  gExposureTrimPreview;\n    float gExposureTrimAnchorExposure0;\n    float gExposureTrimAnchorTrim0;\n    float gExposureTrimAnchorExposure1;\n    float gExposureTrimAnchorTrim1;\n    float gExposureTrimAnchorExposure2;\n    float gExposureTrimAnchorTrim2;\n    float gExposureTrimAnchorExposure3;\n    float gExposureTrimAnchorTrim3;\n    float gExposureTrimAnchorExposure4;\n    float gExposureTrimAnchorTrim4;\n    float gExposureTrimAnchorExposure5;\n    float gExposureTrimAnchorTrim5;\n    float gExposureTrimAnchorExposure6;\n    float gExposureTrimAnchorTrim6;\n    float gExposureTrimAnchorExposure7;\n    float gExposureTrimAnchorTrim7;\n"""
s = replace_once(
    s,
    "    float gExposureTrim;\n"
    "    uint  gUseExposureWhitePoint;\n"
    "};\n",
    "    float gExposureTrim;\n"
    "    uint  gUseExposureWhitePoint;\n" + anchor_fields_hlsl + "};\n",
    "HLSL automatic trim anchor constants",
)
wp_marker = "float AutomaticExposureWhitePoint()\n" if "float AutomaticExposureWhitePoint()\n" in s else "float EffectiveWhitePoint()\n"
wp_helpers = r'''float ExposureTrimAnchorExposure(uint index)
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

float EffectiveExposureTrim(float exposure)
{
    const float fallback = clamp(gExposureTrim, 0.25, 10.0);
    const uint count = min(gExposureTrimAnchorCount, 8u);

    if (gExposureTrimPreview != 0 || count == 0 || !isfinite(exposure) || exposure <= 1e-8)
        return fallback;

    if (count == 1)
        return clamp(ExposureTrimAnchorValue(0), 0.25, 10.0);

    const float firstExposure = ExposureTrimAnchorExposure(0);
    const float lastExposure = ExposureTrimAnchorExposure(count - 1);

    if (exposure <= firstExposure)
        return clamp(ExposureTrimAnchorValue(0), 0.25, 10.0);
    if (exposure >= lastExposure)
        return clamp(ExposureTrimAnchorValue(count - 1), 0.25, 10.0);

    [unroll] for (uint i = 0; i < 7u; ++i)
    {
        if (i + 1 >= count)
            break;

        const float aExposure = ExposureTrimAnchorExposure(i);
        const float bExposure = ExposureTrimAnchorExposure(i + 1);

        if (exposure >= aExposure && exposure <= bExposure && bExposure > aExposure * 1.000001)
        {
            const float aTrim = max(ExposureTrimAnchorValue(i), 0.25);
            const float bTrim = max(ExposureTrimAnchorValue(i + 1), 0.25);
            const float t = (log(exposure) - log(aExposure)) / (log(bExposure) - log(aExposure));
            return clamp(exp(lerp(log(aTrim), log(bTrim), t)), 0.25, 10.0);
        }
    }

    return clamp(ExposureTrimAnchorValue(count - 1), 0.25, 10.0);
}

'''
s = replace_once(s, wp_marker, wp_helpers + wp_marker, "HLSL trim interpolation helpers")
s = replace_once(
    s,
    "        const float trim = clamp(gExposureTrim, 0.25, 10.0);\n",
    "        const float trim = EffectiveExposureTrim(exposure);\n",
    "HLSL use exposure-dependent trim",
)
write(rel, s)

print("DLSS-NR exposure-dependent Trim anchors patch applied")
