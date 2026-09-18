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


# This patch runs after the Auto Exposure, Trim-anchor and FXC compatibility patches.
# Trim anchors are deliberately keyed by the untrimmed white point that the renderer
# actually uses (PreExposure / Exposure), rather than by raw Exposure alone.
# Existing config key names are retained so the release-only patch remains compact.

# -----------------------------------------------------------------------------
# Config comments: document the stored first coordinate correctly.
# -----------------------------------------------------------------------------
rel = "OptiScaler/Config.h"
s = read(rel)
s = replace_once(
    s,
    "    // Exposure-dependent Trim calibration tables, serialized as exposure:trim pairs.\n"
    "    // The two sources are intentionally separate because their exposure scales need not match.\n",
    "    // Base-white-point-dependent Trim calibration tables, serialized as baseWhitePoint:trim pairs.\n"
    "    // The two sources are intentionally separate because their white-point scales need not match.\n",
    "white-point anchor config comments",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Menu: capture, display and preview anchors against Base White Point.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Menu.cpp"
s = read(rel)

s = replace_once(
    s,
    "                        ex.exposure, config->DlssNrWhitePointTrim.value_or_default(), trimAnchors,\n",
    "                        ex.preExposure / ex.exposure, config->DlssNrWhitePointTrim.value_or_default(), trimAnchors,\n",
    "game status trim uses base white point",
)
s = replace_once(
    s,
    "                        autoEx.exposure, config->DlssNrAutoExposureTrim.value_or_default(), trimAnchors,\n",
    "                        autoEx.preExposure / autoEx.exposure, config->DlssNrAutoExposureTrim.value_or_default(), trimAnchors,\n",
    "automatic status trim uses base white point",
)
s = replace_once(
    s,
    "                                             gameStatus.exposure, gameTrim, \"gameExposureTrim\");\n",
    "                                             gameStatus.exposure > 1e-8f\n"
    "                                                 ? gameStatus.preExposure / gameStatus.exposure\n"
    "                                                 : 0.0f,\n"
    "                                             gameTrim, \"gameExposureTrim\");\n",
    "game anchor capture uses base white point",
)
s = replace_once(
    s,
    "                                             autoStatus.exposure, autoTrim, \"automaticExposureTrim\");\n",
    "                                             autoStatus.exposure > 1e-8f\n"
    "                                                 ? autoStatus.preExposure / autoStatus.exposure\n"
    "                                                 : 0.0f,\n"
    "                                             autoTrim, \"automaticExposureTrim\");\n",
    "automatic anchor capture uses base white point",
)

replacements = [
    (
        "\\nexposure, then press Add Anchor point. The preview switch is not saved across restarts.",
        "\\nbase white point, then press Add Anchor point. The preview switch is not saved across restarts.",
        "preview help text",
    ),
    (
        "Waiting for a valid exposure before an Anchor point can be added.",
        "Waiting for a valid base white point before an Anchor point can be added.",
        "waiting text",
    ),
    (
        "Current exposure %.5f -> effective Trim %.2fx%s",
        "Current base white point %.5f -> effective Trim %.2fx%s",
        "current key text",
    ),
    (
        "No Trim Anchor points: the Trim slider is used for every exposure.",
        "No Trim Anchor points: the Trim slider is used for every base white point.",
        "no-anchor text",
    ),
    (
        "1 Trim Anchor point: its Trim is used for every exposure.",
        "1 Trim Anchor point: its Trim is used for every base white point.",
        "one-anchor text",
    ),
    (
        "%u Trim Anchor points: Trim is interpolated between exposure values.",
        "%u Trim Anchor points: Trim is interpolated between base white-point values.",
        "multi-anchor text",
    ),
    (
        "Exposure %.5f  ->  Trim %.2fx",
        "Base white point %.5f  ->  Trim %.2fx",
        "anchor row text",
    ),
    (
        "\\nused for every exposure. With Anchor points it becomes the calibration slider",
        "\\nused for every base white point. With Anchor points it becomes the calibration slider",
        "game trim help",
    ),
    (
        "\\nagainst the current 1x1 automatic exposure in the shader, so scene changes do",
        "\\nagainst the current base white point (PreExposure / Exposure) in the shader, so scene changes do",
        "automatic trim help",
    ),
]
for old, new, label in replacements:
    s = replace_once(s, old, new, label)

write(rel, s)

# -----------------------------------------------------------------------------
# CPU game-exposure path: derive the same untrimmed Base White Point first,
# evaluate the anchor curve on it, then apply Trim.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
s = read(rel)
s = replace_once(
    s,
    "        const auto anchors =\n"
    "            ParseExposureTrimAnchorsRuntime(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float trim = ExposureTrimForRuntime(\n"
    "            g_nr.gameExposure, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "            cfg.DlssNrGameExposureTrimPreview.value_or_default());\n\n"
    "        return std::clamp(g_nr.gamePreExposure / g_nr.gameExposure * trim, 0.01f, 4096.0f);\n",
    "        const auto anchors =\n"
    "            ParseExposureTrimAnchorsRuntime(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float baseWhitePoint = g_nr.gamePreExposure / g_nr.gameExposure;\n"
    "        const float trim = ExposureTrimForRuntime(\n"
    "            baseWhitePoint, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "            cfg.DlssNrGameExposureTrimPreview.value_or_default());\n\n"
    "        return std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n",
    "game runtime trim uses base white point",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Same-frame Automatic exposure path: compute the untrimmed white point in HLSL,
# evaluate the anchor curve there, then multiply by Trim. This retains same-frame
# behavior; CPU readback remains UI/anchor-capture only.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
s = read(rel)
s = replace_once(
    s,
    "        const float trim = EffectiveExposureTrim(exposure);\n"
    "        const float whitePoint = preExposure / exposure * trim;\n",
    "        const float baseWhitePoint = preExposure / exposure;\n"
    "        const float trim = EffectiveExposureTrim(baseWhitePoint);\n"
    "        const float whitePoint = baseWhitePoint * trim;\n",
    "automatic shader trim uses base white point",
)
write(rel, s)

print("DLSS-NR Trim anchors now use Base White Point (PreExposure / Exposure)")
