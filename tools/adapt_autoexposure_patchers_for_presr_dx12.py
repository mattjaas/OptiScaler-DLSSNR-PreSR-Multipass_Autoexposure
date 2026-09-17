from pathlib import Path
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: adapt_autoexposure_patchers_for_presr_dx12.py <patch-tools-dir>")

    path = Path(sys.argv[1]).resolve() / "apply_dlssnr_autoexposure.py"
    script = path.read_text(encoding="utf-8")

    # PreSR/Multipass enters feature creation immediately after the meter allocation,
    # whereas the source fork has an unrelated state-restoration comment there. Make
    # the insertion depend only on the meter block itself; its body is identical.
    old = """needle = '''        if (g_nr.meter != nullptr)
            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);
    }

    // On an engine that needs its compute state put back'''
replacement = '''        if (g_nr.meter != nullptr)
            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);
    }

    if (g_nr.autoExposure == nullptr)
    {
        g_nr.autoExposure = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1);
        g_nr.autoExposureReadable = false;

        if (g_nr.autoExposure == nullptr)
            LOG_WARN(\"DLSS-NR: could not allocate the 1x1 auto-exposure texture\");
        else
            LOG_INFO(\"DLSS-NR: GPU auto exposure is available\");
    }

    // On an engine that needs its compute state put back'''"""

    new = """needle = '''        if (g_nr.meter != nullptr)
            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);
    }
'''
replacement = '''        if (g_nr.meter != nullptr)
            LOG_INFO(\"DLSS-NR: white point meter up, {}x{} tiles\", kDlssNrMeterGrid, kDlssNrMeterGrid);
    }

    if (g_nr.autoExposure == nullptr)
    {
        g_nr.autoExposure = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1);
        g_nr.autoExposureReadable = false;

        if (g_nr.autoExposure == nullptr)
            LOG_WARN(\"DLSS-NR: could not allocate the 1x1 auto-exposure texture\");
        else
            LOG_INFO(\"DLSS-NR: GPU auto exposure is available\");
    }
'''"""

    script = replace_once(script, old, new, "DX12 auto-exposure allocation anchor in patcher")
    path.write_text(script, encoding="utf-8", newline="\n")
    print(f"Adapted DX12 allocation anchor for PreSR/Multipass: {path}")


if __name__ == "__main__":
    main()
