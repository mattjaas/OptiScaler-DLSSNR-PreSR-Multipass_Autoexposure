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


# Runs after the Auto Exposure, Trim-anchor, FXC-compatibility and Base White Point patches.
# Automatic exposure keeps the original linear arithmetic-average exposure formula. Every tile
# remains in the meter, while the user-controlled Shadow protection from bright highlights slider
# smoothly compresses bright outliers relative to a stable full-frame log-luminance reference.
# There are no histogram buckets and no hard include/exclude decisions, avoiding exposure jumps
# when the camera moves slightly. 0% restores the exact old full-frame arithmetic average;
# 100% starts compression at +1 EV with a 0.35 EV slope. The default is 100%.
#
# The slider maps linearly:
#   protection = 0%   -> knee +3 EV, slope 1.00 (exact old average path)
#   protection = 100% -> knee +1 EV, slope 0.35
# Intermediate values linearly interpolate both parameters.

# -----------------------------------------------------------------------------
# Config: persisted shadow-protection strength.
# -----------------------------------------------------------------------------
rel = "OptiScaler/Config.h"
s = read(rel)
s = replace_once(
    s,
    "    CustomOptional<float> DlssNrAutoExposureTrim { 1.0f };\n\n"
    "    // Base-white-point-dependent Trim calibration tables, serialized as baseWhitePoint:trim pairs.\n",
    "    CustomOptional<float> DlssNrAutoExposureTrim { 1.0f };\n"
    "    CustomOptional<float> DlssNrAutoExposureShadowProtection { 100.0f };\n\n"
    "    // Base-white-point-dependent Trim calibration tables, serialized as baseWhitePoint:trim pairs.\n",
    "auto exposure shadow protection config field",
)
write(rel, s)

rel = "OptiScaler/Config.cpp"
s = read(rel)
s = replace_once(
    s,
    "            DlssNrAutoExposureTrim.set_from_config(readFloat(\"DlssNr\", \"AutoExposureTrim\"));\n"
    "            DlssNrGameExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"GameExposureTrimAnchors\"));\n",
    "            DlssNrAutoExposureTrim.set_from_config(readFloat(\"DlssNr\", \"AutoExposureTrim\"));\n"
    "            DlssNrAutoExposureShadowProtection.set_from_config(\n"
    "                readFloat(\"DlssNr\", \"AutoExposureShadowProtection\"));\n"
    "            DlssNrGameExposureTrimAnchors.set_from_config(readString(\"DlssNr\", \"GameExposureTrimAnchors\"));\n",
    "read auto exposure shadow protection",
)
s = replace_once(
    s,
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrim\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"GameExposureTrimAnchors\",\n",
    "    ini.SetValue(\"DlssNr\", \"AutoExposureTrim\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureTrim.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"AutoExposureShadowProtection\",\n"
    "                 GetFloatValue(Instance()->DlssNrAutoExposureShadowProtection.value_for_config()).c_str());\n"
    "    ini.SetValue(\"DlssNr\", \"GameExposureTrimAnchors\",\n",
    "write auto exposure shadow protection",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Menu: shadow-protection slider + Trim range 0.25x..50x for game and auto.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Menu.cpp"
s = read(rel)
s = replace_once(
    s,
    "            const auto autoStatus = DlssNr::AutoExposureStatus();\n"
    "            RenderExposureTrimAnchorControls(config->DlssNrAutoExposureTrimAnchors,\n",
    "            float shadowProtection =\n"
    "                config->DlssNrAutoExposureShadowProtection.value_or_default();\n"
    "            if (ImGui::SliderFloat(\"Shadow protection from bright highlights\", &shadowProtection,\n"
    "                                   0.0f, 100.0f, \"%.0f%%\"))\n"
    "                config->DlssNrAutoExposureShadowProtection = shadowProtection;\n\n"
    "            HelpMarker(\"Controls how strongly very bright highlights are prevented from driving\"\n"
    "                       \"\\nAutomatic exposure upward and raising White Point over darker scene areas.\"\n"
    "                       \"\\nEvery 64x64 tile remains part of the meter; highlights are compressed,\"\n"
    "                       \"\\nnot removed. The final luminance remains a pixel-area-weighted linear\"\n"
    "                       \"\\narithmetic mean and the original exposure formula is unchanged.\"\n"
    "                       \"\\n\\n0%: original full-frame arithmetic average (knee +3 EV, slope 1.00).\"\n"
    "                       \"\\n100%: strongest protection (knee +1 EV, slope 0.35).\"\n"
    "                       \"\\nIntermediate values linearly interpolate both the knee and slope.\");\n\n"
    "            ImGui::TextDisabled(\"Metering: highlight-compressed arithmetic average.\");\n\n"
    "            const auto autoStatus = DlssNr::AutoExposureStatus();\n"
    "            RenderExposureTrimAnchorControls(config->DlssNrAutoExposureTrimAnchors,\n",
    "automatic exposure shadow protection slider",
)

range_count = s.count("0.25f, 10.0f")
if range_count != 8:
    raise RuntimeError(f"menu Trim range: expected 8 occurrences, got {range_count}")
s = s.replace("0.25f, 10.0f", "0.25f, 50.0f")
help_count = s.count("Range: 0.25x to 10.00x.")
if help_count != 2:
    raise RuntimeError(f"menu Trim help range: expected 2 occurrences, got {help_count}")
s = s.replace("Range: 0.25x to 10.00x.", "Range: 0.25x to 50.00x.")
write(rel, s)

# -----------------------------------------------------------------------------
# Constant-buffer strength, C++ dispatch value and 50x runtime Trim range.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Common.h"
s = read(rel)
s = replace_once(
    s,
    "    float ExposureTrimAnchorExposure7;\n"
    "    float ExposureTrimAnchorTrim7;\n"
    "};\n",
    "    float ExposureTrimAnchorExposure7;\n"
    "    float ExposureTrimAnchorTrim7;\n"
    "    float AutoExposureShadowProtection;\n"
    "};\n",
    "automatic exposure shadow protection constant",
)
write(rel, s)

rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
s = read(rel)
if "        const D3D12_RESOURCE_DESC exposureSourceDesc = source->GetDesc();\n" in s:
    s = replace_once(
        s,
        "        exposureParams.PreExposure = frame.PreExposure;\n"
        "        const D3D12_RESOURCE_DESC exposureSourceDesc = source->GetDesc();\n",
        "        exposureParams.PreExposure = frame.PreExposure;\n"
        "        exposureParams.AutoExposureShadowProtection =\n"
        "            std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);\n"
        "        const D3D12_RESOURCE_DESC exposureSourceDesc = source->GetDesc();\n",
        "automatic exposure shadow protection dispatch constant",
    )
else:
    s = replace_once(
        s,
        "        exposureParams.PreExposure = frame.PreExposure;\n"
        "        const D3D12_RESOURCE_DESC exposureSourceDesc = target->GetDesc();\n",
        "        exposureParams.PreExposure = frame.PreExposure;\n"
        "        exposureParams.AutoExposureShadowProtection =\n"
        "            std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);\n"
        "        const D3D12_RESOURCE_DESC exposureSourceDesc = target->GetDesc();\n",
        "automatic exposure shadow protection dispatch constant multipass",
    )
runtime_range_count = s.count("0.25f, 10.0f")
if runtime_range_count not in (4, 5):
    raise RuntimeError(f"runtime Trim range: expected 4 or 5 occurrences, got {runtime_range_count}")
s = s.replace("0.25f, 10.0f", "0.25f, 50.0f")
write(rel, s)

# -----------------------------------------------------------------------------
# HLSL: user-controlled soft highlight compression. All tiles remain in the final
# LINEAR arithmetic mean. A full-frame mean in log2 luminance is used only as a
# smooth reference. The 0..100% slider maps linearly from knee +3 EV / slope 1.00
# to knee +1 EV / slope 0.35. At exactly 0%, use the exact old arithmetic-average
# path so the slider fully replaces the former emergency-fallback checkbox.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
s = read(rel)
s = replace_once(
    s,
    "    float gExposureTrimAnchorExposure7;\n"
    "    float gExposureTrimAnchorTrim7;\n"
    "};\n",
    "    float gExposureTrimAnchorExposure7;\n"
    "    float gExposureTrimAnchorTrim7;\n"
    "    float gAutoExposureShadowProtection;\n"
    "};\n",
    "HLSL automatic exposure shadow protection constant",
)

hlsl_range_count = s.count("0.25, 10.0")
if hlsl_range_count != 11:
    raise RuntimeError(f"HLSL Trim range: expected 11 occurrences, got {hlsl_range_count}")
s = s.replace("0.25, 10.0", "0.25, 50.0")

auto_mode_id = 12 if "    if (gMode == 12)\n    {\n" in s else 5
auto_token = f"    if (gMode == {auto_mode_id})\n    {{\n"
auto_start = s.find(auto_token)
auto_end = s.find("    if (gMode == 4)\n", auto_start)
if auto_start < 0 or auto_end < 0 or auto_end <= auto_start:
    raise RuntimeError("automatic exposure HLSL block was not found")
if s.find(auto_token, auto_start + 1) >= 0:
    raise RuntimeError("automatic exposure HLSL block appears more than once")

new_auto = r'''    // Automatic exposure. Keep the original linear arithmetic-average exposure math, but make
    // bright HDR highlights progressively less dominant according to the user-controlled protection.
    // 0% is the exact old full-frame arithmetic average. 100% starts soft compression at +1 EV
    // above the smooth log-luminance reference, with additional brightness growing at 35% of its
    // original EV rate. Intermediate values linearly interpolate knee (+3 -> +1 EV) and slope
    // (1.00 -> 0.35). No tiles are removed and there are no histogram/percentile boundary switches.
    if (gMode == 5)
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

        // First pass: always preserve the exact old arithmetic accumulator. The log-domain
        // reference is only needed when protection is above zero.
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
                    const float logLuma = clamp(log2(sceneLuma), -24.0, 24.0);
                    weightedSceneLogLuma += logLuma * pixels;
                }
            }
        }

        const float averageBufferLuma = totalPixels > 0.0 ? weightedBufferLuma / totalPixels : 0.0;
        const float simpleAverageSceneLuma = averageBufferLuma / preExposure;
        float meteredSceneLuma = simpleAverageSceneLuma;

        if (protection > 0.0 && totalPixels > 0.0)
        {
            const float referenceLogLuma = weightedSceneLogLuma / totalPixels;
            const float highlightKneeEv = lerp(3.0, 1.0, protection);
            const float highlightCompressionSlope = lerp(1.0, 0.35, protection);
            float protectedLinearSum = 0.0;

            // Second pass: every tile contributes by real pixel area. Only strong bright values
            // above the interpolated knee are softly compressed.
            [loop] for (uint protectedTy = 0u; protectedTy < 64u; ++protectedTy)
            {
                const uint y0 = (protectedTy * srcH) / 64u;
                const uint y1 = ((protectedTy + 1u) * srcH) / 64u;
                const uint tileH = max(y1 - y0, 1u);

                [loop] for (uint protectedTx = 0u; protectedTx < 64u; ++protectedTx)
                {
                    const uint x0 = (protectedTx * srcW) / 64u;
                    const uint x1 = ((protectedTx + 1u) * srcW) / 64u;
                    const uint tileW = max(x1 - x0, 1u);
                    const float pixels = (float) tileW * (float) tileH;
                    const float tileMean = max(
                        SanitizeFinite(gSource.Load(int3(protectedTx, protectedTy, 0)).r, 0.0), 0.0);
                    const float sceneLuma = max(tileMean / preExposure, 1e-8);
                    const float logLuma = clamp(log2(sceneLuma), -24.0, 24.0);
                    const float deltaEv = logLuma - referenceLogLuma;

                    float compressedLogLuma = logLuma;
                    if (deltaEv > highlightKneeEv)
                    {
                        compressedLogLuma = referenceLogLuma + highlightKneeEv +
                            (deltaEv - highlightKneeEv) * highlightCompressionSlope;
                    }

                    const float compressedSceneLuma = exp2(clamp(compressedLogLuma, -24.0, 24.0));
                    protectedLinearSum += compressedSceneLuma * pixels;
                }
            }

            const float protectedAverageSceneLuma = protectedLinearSum / totalPixels;
            if (isfinite(protectedAverageSceneLuma) && protectedAverageSceneLuma > 1e-8)
                meteredSceneLuma = protectedAverageSceneLuma;
        }

        // Keep the original NVIDIA-style exposure formula unchanged.
        float exposure = meteredSceneLuma > 1e-8
            ? 0.18 / (meteredSceneLuma * 0.82)
            : 1.0;

        if (!isfinite(exposure) || exposure <= 0.0)
            exposure = 1.0;

        gTarget[uint2(0, 0)] = float4(exposure, 0.0, 0.0, 1.0);
        return;
    }

'''
new_auto = new_auto.replace("if (gMode == 5)", f"if (gMode == {auto_mode_id})")
s = s[:auto_start] + new_auto + s[auto_end:]
write(rel, s)

print("DLSS-NR shadow-protection slider Automatic exposure + 50x Trim patch applied")
