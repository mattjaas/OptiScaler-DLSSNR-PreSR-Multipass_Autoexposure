from pathlib import Path

HERE = Path(__file__).resolve()
_candidates = [Path.cwd(), HERE.parent]
if len(HERE.parents) >= 3:
    _candidates.append(HERE.parents[2])
ROOT = next((p for p in _candidates if (p / "OptiScaler").is_dir()), None)
if ROOT is None:
    raise RuntimeError("Run this script from the repository root (the directory containing OptiScaler).")

rel = "OptiScaler/dlssnr/DlssNrFeature_Vk.cpp"
path = ROOT / rel
s = path.read_text(encoding="utf-8-sig")
old = "std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 10.0f)"
new = "std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 50.0f)"
count = s.count(old)
if count != 1:
    raise RuntimeError(f"Vulkan game Trim range: expected exactly one 10x clamp, got {count}")
s = s.replace(old, new, 1)
path.write_text(s, encoding="utf-8", newline="\n")
print("DLSS-NR Vulkan game Trim range extended to 50x")

# This patch runs after the automatic-exposure and Trim-anchor patches, so enforce
# the final Automatic Exposure Trim default here without changing the checked-in sources.
rel = "OptiScaler/Config.h"
path = ROOT / rel
s = path.read_text(encoding="utf-8-sig")
old = "    CustomOptional<float> DlssNrAutoExposureTrim { 1.0f };"
new = "    CustomOptional<float> DlssNrAutoExposureTrim { 5.0f };"
count = s.count(old)
if count != 1:
    raise RuntimeError(f"Automatic Exposure Trim default: expected exactly one 1x default, got {count}")
s = s.replace(old, new, 1)
path.write_text(s, encoding="utf-8", newline="\n")

rel = "OptiScaler/dlssnr/DlssNr_Menu.cpp"
path = ROOT / rel
s = path.read_text(encoding="utf-8-sig")
old = """            if (ImGui::SmallButton(\"Reset##autoexposuretrim\"))
            {
                config->DlssNrAutoExposureTrim = 1.0f;
                autoTrim = 1.0f;
            }
"""
new = """            if (ImGui::SmallButton(\"Reset##autoexposuretrim\"))
            {
                config->DlssNrAutoExposureTrim = 5.0f;
                autoTrim = 5.0f;
            }
"""
count = s.count(old)
if count != 1:
    raise RuntimeError(f"Automatic Exposure Trim reset: expected exactly one 1x reset block, got {count}")
s = s.replace(old, new, 1)
path.write_text(s, encoding="utf-8", newline="\n")
print("DLSS-NR Automatic Exposure Trim default/reset changed to 5x")
