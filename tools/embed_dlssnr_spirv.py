from pathlib import Path

HERE = Path(__file__).resolve()
_candidates = [Path.cwd(), HERE.parent]
if len(HERE.parents) >= 3:
    _candidates.append(HERE.parents[2])
ROOT = next((p for p in _candidates if (p / "OptiScaler").is_dir()), None)
if ROOT is None:
    raise RuntimeError("Run this script from the repository root (the directory containing OptiScaler).")

spv = ROOT / "OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.spv"
header = ROOT / "OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader_Vk.h"

data = spv.read_bytes()
if len(data) < 20 or data[:4] != b"\x03\x02\x23\x07":
    raise RuntimeError("DlssNr_Shader.spv is missing or is not a SPIR-V module")

lines = []
for offset in range(0, len(data), 12):
    chunk = data[offset : offset + 12]
    lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

text = "#pragma once\n\ninline static const unsigned char dlssnr_spv[] = {\n"
text += "\n".join(lines)
text += "\n};\n"
header.write_text(text, encoding="utf-8", newline="\n")

print(f"Embedded {len(data)} SPIR-V bytes into {header}")
