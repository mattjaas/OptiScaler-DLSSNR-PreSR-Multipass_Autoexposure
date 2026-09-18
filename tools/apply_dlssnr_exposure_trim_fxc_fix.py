from pathlib import Path

HERE = Path(__file__).resolve()
_candidates = [Path.cwd(), HERE.parent]
if len(HERE.parents) >= 3:
    _candidates.append(HERE.parents[2])
ROOT = next((p for p in _candidates if (p / "OptiScaler").is_dir()), None)
if ROOT is None:
    raise RuntimeError("Run this script from the repository root (the directory containing OptiScaler).")

# The anchor patch replaces NrState::meterExposureValid with meterExposureKind, but the
# original Auto Exposure patch has a second cleanup loop later in DlssNr_Dx12.cpp. Replace
# that remaining loop after both earlier patches have run.
dx_rel = "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
dx_path = ROOT / dx_rel
dx = dx_path.read_text(encoding="utf-8-sig")
old_cleanup = "    for (bool& valid : g_nr.meterExposureValid)\n        valid = false;\n"
new_cleanup = "    for (unsigned int& kind : g_nr.meterExposureKind)\n        kind = 0u;\n"
cleanup_count = dx.count(old_cleanup)
if cleanup_count != 1:
    raise RuntimeError(f"remaining meter exposure cleanup: expected exactly one match, got {cleanup_count}")
dx = dx.replace(old_cleanup, new_cleanup, 1)
dx_path.write_text(dx, encoding="utf-8", newline="\n")

# FXC can overflow its own stack when the first version of the 8-anchor helper is aggressively
# expanded. Replace it with an equivalent explicit seven-segment implementation.
rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
path = ROOT / rel
s = path.read_text(encoding="utf-8-sig")

old_start = s.find("float ExposureTrimAnchorExposure(uint index)\n")
old_end = s.find("float AutomaticExposureWhitePoint()\n")
if old_end < 0:
    old_end = s.find("float EffectiveWhitePoint()\n")
if old_start < 0 or old_end < 0 or old_end <= old_start:
    raise RuntimeError("FXC trim-anchor helper block was not found exactly once")
if s.find("float ExposureTrimAnchorExposure(uint index)\n", old_start + 1) >= 0:
    raise RuntimeError("FXC trim-anchor helper block appears more than once")

new = r'''float InterpolateExposureTrimSegment(float exposure, float aExposure, float aTrim,
                                     float bExposure, float bTrim)
{
    aTrim = max(aTrim, 0.25);
    bTrim = max(bTrim, 0.25);
    const float t = (log(exposure) - log(aExposure)) / (log(bExposure) - log(aExposure));
    return clamp(exp(lerp(log(aTrim), log(bTrim), t)), 0.25, 10.0);
}

float EffectiveExposureTrim(float exposure)
{
    const float fallback = clamp(gExposureTrim, 0.25, 10.0);
    const uint count = min(gExposureTrimAnchorCount, 8u);

    if (gExposureTrimPreview != 0 || count == 0 || !isfinite(exposure) || exposure <= 1e-8)
        return fallback;

    if (count == 1)
        return clamp(gExposureTrimAnchorTrim0, 0.25, 10.0);

    if (exposure <= gExposureTrimAnchorExposure0)
        return clamp(gExposureTrimAnchorTrim0, 0.25, 10.0);

    if (count >= 2 && exposure <= gExposureTrimAnchorExposure1)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure0,
                                              gExposureTrimAnchorTrim0, gExposureTrimAnchorExposure1,
                                              gExposureTrimAnchorTrim1);
    if (count >= 3 && exposure <= gExposureTrimAnchorExposure2)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure1,
                                              gExposureTrimAnchorTrim1, gExposureTrimAnchorExposure2,
                                              gExposureTrimAnchorTrim2);
    if (count >= 4 && exposure <= gExposureTrimAnchorExposure3)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure2,
                                              gExposureTrimAnchorTrim2, gExposureTrimAnchorExposure3,
                                              gExposureTrimAnchorTrim3);
    if (count >= 5 && exposure <= gExposureTrimAnchorExposure4)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure3,
                                              gExposureTrimAnchorTrim3, gExposureTrimAnchorExposure4,
                                              gExposureTrimAnchorTrim4);
    if (count >= 6 && exposure <= gExposureTrimAnchorExposure5)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure4,
                                              gExposureTrimAnchorTrim4, gExposureTrimAnchorExposure5,
                                              gExposureTrimAnchorTrim5);
    if (count >= 7 && exposure <= gExposureTrimAnchorExposure6)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure5,
                                              gExposureTrimAnchorTrim5, gExposureTrimAnchorExposure6,
                                              gExposureTrimAnchorTrim6);
    if (count >= 8 && exposure <= gExposureTrimAnchorExposure7)
        return InterpolateExposureTrimSegment(exposure, gExposureTrimAnchorExposure6,
                                              gExposureTrimAnchorTrim6, gExposureTrimAnchorExposure7,
                                              gExposureTrimAnchorTrim7);

    if (count == 2) return clamp(gExposureTrimAnchorTrim1, 0.25, 10.0);
    if (count == 3) return clamp(gExposureTrimAnchorTrim2, 0.25, 10.0);
    if (count == 4) return clamp(gExposureTrimAnchorTrim3, 0.25, 10.0);
    if (count == 5) return clamp(gExposureTrimAnchorTrim4, 0.25, 10.0);
    if (count == 6) return clamp(gExposureTrimAnchorTrim5, 0.25, 10.0);
    if (count == 7) return clamp(gExposureTrimAnchorTrim6, 0.25, 10.0);
    return clamp(gExposureTrimAnchorTrim7, 0.25, 10.0);
}

'''

s = s[:old_start] + new + s[old_end:]
path.write_text(s, encoding="utf-8", newline="\n")
print("DLSS-NR exposure Trim FXC/C++ compatibility fixes applied")
