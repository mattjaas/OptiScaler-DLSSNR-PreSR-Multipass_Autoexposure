"""Integration contract for the fixed Superior Scaling path and preserved laboratory modes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def block(text, start):
    first = text.index("{", text.index(start))
    depth = 1
    end = first + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[first:end]


def test_superior():
    policy = read("OptiScaler/dlssnr/DlssNr_SuperiorScaling.h")
    assert "Transfer = 11u" in policy and "preset == 11 ? 11 : 1" in policy
    assert ".8f, .75f, .95f" in policy and ".661f, .787f, .784f" in policy
    menu = read("OptiScaler/dlssnr/DlssNr_MenuControls.cpp")
    assert '"Temporal image-anchored residual + P100-guided", "Superior Scaling"' in menu
    panel = block(menu, "if (SuperiorScaling::Enabled(config->DlssNrTransfer.value_or_default()))")
    assert 'ImGui::Combo("DLSS preset"' in panel and "return;" in panel
    assert "Slider" not in panel and "EffectiveGuided" not in panel
    assert '"A - Faster", "K - More stable NR in motion"' in panel
    # Shared input/HDR settings must remain reachable after the Superior preset panel returns.
    enlargement_panel = block(menu, "static void RenderEnlargement(Config* config)")
    input_panel = block(menu, "void RenderInput(Config* config)")
    assert 'ImGui::Combo("Enlargement"' in enlargement_panel
    assert "RenderEnlargement(config);" in input_panel and "return;" not in input_panel
    for label in ("HDR mapping (experimental)", "White point source", "Exposure trim",
                  "Highlight protection", "Trim anchors", "Paper white"):
        assert f'"{label}"' in input_panel and f'"{label}"' not in enlargement_panel
    main_menu = read("OptiScaler/dlssnr/DlssNr_Menu.cpp")
    assert "Legacy inter-pass benchmark lab" not in main_menu
    lab = block(main_menu, "if (!SuperiorScaling::Enabled(*config))")
    assert 'ImGui::InputInt("Benchmark warmup samples"' in lab
    assert "StartInterPassBenchmark" in lab
    assert "interPassAvailable && !SuperiorScaling::Enabled(*config)" in menu
    profiles = read("OptiScaler/dlssnr/PassProfiles.h")
    base = profiles[profiles.index("inline GuidedResidualGains GuidedResidualBaseGains"):]
    assert base.index("SuperiorScaling::High(style)") < base.index("if (shapingMode == 0u)")
    assert "SuperiorScaling::Enabled(cfg) || cfg.DlssNrGuidedResidualCompoundPasses" in profiles
    assert "!SuperiorScaling::Enabled(cfg) && cfg.DlssNrInterPassNeutralLow" in profiles
    enlarge = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12_Enlarge.cpp")
    encode = block(enlarge, "            if (superior)")
    assert "DlssNrMode_SuperiorCarrierEncode" in encode and "DispatchPassAux2" not in encode
    assert not any(word in encode for word in ("carrierApplied", "TemporalCarrierLimits", "readback", "configuredMargin"))
    assert "if (residualDlaa && !superior)" in enlarge  # No analysis allocations in Superior.
    assert "if (!superior)" in enlarge[enlarge.index("// Non-blocking telemetry:") - 40:]
    assert "superior || cfg.DlssNrTemporalDlaaIsHdr" in enlarge
    assert "superior ? true : transfer == 8u" in enlarge
    assert "superior ? 0x800u" in enlarge  # Mode changes recreate the private temporal feature.
    shader = read("OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl")
    carrier = block(shader, "void SuperiorCarrierMain")
    assert "precise float3 residual = model - proxy" in carrier
    assert "precise float3 encoded = 0.5 + residual" in carrier
    assert "clamp(encoded, -1.0, 2.0)" in carrier
    assert "filtered - 0.5" in carrier
    assert not any(word in carrier for word in ("gAux2", "TemporalCarrierRange", "gKeep", "headroom"))
    assert shader.count("#if !defined(DLSSNR_SUPERIOR_SCALING)") == 2
    run = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp")
    assert "nr.interPassSuperior != interPassSuperior" in run
    assert "if (DlssNr::SuperiorScaling::Enabled(cfg)) spatialSettings.enabled = false;" in run
    assert "transfer >= 7u && transfer <= 11u" in run
    final = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp")
    assert "configuredTransfer <= 11u" in final and "DlssNr::SuperiorScaling::ShaderFlag" in final
    assert "shaping.high == 1.0f && shaping.low == 1.0f" in final
    config = read("OptiScaler/Config.cpp")
    assert 'readInt("DlssNr", "SuperiorScalingPreset")' in config
    assert 'ini.SetValue("DlssNr", "SuperiorScalingPreset"' in config
    compile_script = read("tools/compile_nr_interpass.ps1")
    assert '"SuperiorCarrierMain"' in compile_script and '"carrier", "resolve", "cross5"' in compile_script
    print("Superior integration PASS: fixed settings, A/K-only UI, no analysis dispatch/resources/readbacks, shadow-free PSOs, history and legacy isolation")


if __name__ == "__main__":
    test_superior()
