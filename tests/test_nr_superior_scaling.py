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
    assert not any(word in carrier for word in ("gAux2", "TemporalCarrierRange", "headroom"))
    motion = block(carrier, "if (gMode == 39u)")
    assert carrier.count("gKeep[") == motion.count("gKeep[") == 1
    assert "gOriginal.Load(int3(mp, 0)).xy * float2(gMvScaleX, gMvScaleY)" in motion
    assert "all(isfinite(mv)) ? mv : float2(0, 0)" in motion
    assert "gMode == 37u || gMode == 39u" in carrier
    assert "superior && reusableDepth && reusableDepth->texture && depth && motion" in enlarge
    assert "reusableDepth->source == depth, rd.Format == DXGI_FORMAT_R32_FLOAT" in enlarge
    assert "regions.motion.valid() && DlssNr::SuperiorScaling::ReuseDepth" in enlarge
    assert "if (needsPrivateGuides && !reuseDepth)" in enlarge
    assert "reuseDepth ? reusableDepth->texture : g.depth.Get()" in enlarge
    assert "reuseDepth ? motion : nullptr" in encode and "reuseDepth ? g.motion.Get() : nullptr" in encode
    assert enlarge.count("if (!reuseDepth)") == 3  # allocation and both depth transitions
    assert shader.count("#if !defined(DLSSNR_SUPERIOR_SCALING)") == 2
    assert "#if defined(DLSSNR_SUPERIOR_RESOLVE)" in shader
    guided = block(shader, "float3 P100GuidedResidualAt(float2 uvq")
    assert "const int radius = 1;" in guided
    assert "[loop] for (int oy = -1; oy <= 1; ++oy)" in guided
    assert "[loop] for (int ox = -1; ox <= 1; ++ox)" in guided
    assert "[unroll]" not in guided
    assert "wRange * wSpatial" in guided  # No exp2-of-sum approximation or changed weights.
    pipelines = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp")
    select = block(pipelines, "ID3D12PipelineState* DlssNr_Dx12::SuperiorPipeline")
    assert "constants.Mode != DlssNrMode_Resolve || constants.ResidualHistoryValid != 1u" in select
    assert "SuperiorScaling::NormalResolve(constants, d.Width, d.Height)" in select
    assert "3u + DlssNr::SuperiorScaling::NormalColorProfile(constants)" in select
    assert "normalResolve && index > 3u ? get(3u) : nullptr" in select
    assert "SuperiorScaling::InputEncode(constants, d.Width, d.Height)" in select
    assert "const unsigned index = inputEncode ? 7u" in select
    assert "SuperiorScaling::LowResidual(constants, d.Width, d.Height)" in select
    assert "lowResidual ? 8u" in select
    # Failed low PSO takes the generic 8x8 dispatch, never the 8x4 specialization geometry.
    assert "pipeline == _superiorPipeline[8] ? DlssNr::SuperiorScaling::LowGroupHeight : _numThreadsY" in pipelines
    assert "(InConstants.Height + threadsY - 1) / threadsY" in pipelines
    assert "LowGroupHeight = 4u" in policy
    low = block(policy, "inline bool LowResidual")
    for guard in ("c.Mode == 18u", "c.Transfer == 1u", "c.DirectResolveFlags == ShaderFlag",
                  "sourceWidth != 0u", "sourceHeight != 0u",
                  "(sourceWidth - 1u) / 8u + 1u", "(sourceHeight - 1u) / 8u + 1u"):
        assert guard in low
    normal = block(policy, "inline bool NormalResolve")
    for guard in ("c.DebugView == 0u", "c.CompareMode == 0u", "c.ShowSkinMask == 0u", "c.ApplyModel != 0u",
                  "c.DirectResolveFlags == ShaderFlag", "sourceWidth <= c.Width",
                  "sourceHeight <= c.Height", "sourceWidth != c.Width || sourceHeight != c.Height"):
        assert guard in normal
    normal_defines = shader.split("#if defined(DLSSNR_SUPERIOR_NORMAL)", 1)[1].split("#endif", 1)[0]
    for define in ("gTransfer 9u", "gDebugView 0u", "gCompareMode 0u", "gApplyModel 1u",
                   "gDirectResolveFlags 65536u"):
        assert f"#define {define}" in normal_defines
    run = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp")
    assert "nr.interPassSuperior != interPassSuperior" in run
    assert "low.DirectResolveFlags = DlssNr::SuperiorScaling::ShaderFlag;" in run
    assert "matchedGuides ? nr.depthSmall : nullptr" in run and "&matchedDepth" in run
    assert "if (DlssNr::SuperiorScaling::Enabled(cfg)) spatialSettings.enabled = false;" in run
    assert "transfer >= 7u && transfer <= 11u" in run
    final = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp")
    assert "if (DlssNr::SuperiorScaling::Enabled(cfg))\n        encodeParams.DirectResolveFlags = DlssNr::SuperiorScaling::ShaderFlag;" in final
    input_encode = block(policy, "inline bool InputEncode")
    for guard in ("c.Mode == 0u", "c.DirectResolveFlags == ShaderFlag", "c.Passthrough <= 1u",
                  "c.ReversibleMode <= 4u", "c.Width != 0u", "c.Height != 0u",
                  "sourceWidth == c.Width", "sourceHeight == c.Height"):
        assert guard in input_encode
    assert "configuredTransfer <= 11u" in final and "DlssNr::SuperiorScaling::ShaderFlag" in final
    assert "shaping.high == 1.0f && shaping.low == 1.0f" in final
    config = read("OptiScaler/Config.cpp")
    assert 'readInt("DlssNr", "SuperiorScalingPreset")' in config
    assert 'ini.SetValue("DlssNr", "SuperiorScalingPreset"' in config
    compile_script = read("tools/compile_nr_interpass.ps1")
    assert '"SuperiorCarrierMain"' in compile_script and '"carrier", "resolve", "cross5"' in compile_script
    assert 'if ($variant -eq "resolve" -or $normal)' in compile_script and '"DLSSNR_SUPERIOR_RESOLVE=1"' in compile_script
    assert '"DLSSNR_SUPERIOR_NORMAL=1"' in compile_script
    assert '"DLSSNR_SUPERIOR_ENCODE=1"' in compile_script
    assert "#if defined(DLSSNR_SUPERIOR_ENCODE)\n#define gMode 0u" in shader
    assert '"DLSSNR_SUPERIOR_LOW=1"' in compile_script
    low_defines = shader.split("#if defined(DLSSNR_SUPERIOR_LOW)", 1)[1].split("#endif", 1)[0]
    assert "#define gMode 18u" in low_defines and "#define gTransfer 1u" in low_defines
    assert "#if defined(DLSSNR_SUPERIOR_LOW)\n[numthreads(8, 4, 1)]\n#else\n[numthreads(8, 8, 1)]" in shader
    assert '"DLSSNR_SUPERIOR_SDR=1"' in compile_script and '"DLSSNR_SUPERIOR_HDR=$($variant.Substring(3))"' in compile_script
    colors = block(policy, "inline uint32_t NormalColorProfile")
    assert "c.Passthrough == 1u" in colors and "c.Passthrough != 0u" in colors and "c.ReversibleMode > 4u" in colors
    assert "c.ReversibleMode == 2u ? 2u : c.ReversibleMode == 4u ? 3u : 0u" in colors
    assert "c.ModelWorkScale < .999f && c.ReplaceDetailStrength > 0.0f" in colors
    assert "#define gPassthrough 1u" in shader and "#define gReversibleMode DLSSNR_SUPERIOR_HDR" in shader
    print("Superior integration PASS: fixed settings, A/K-only UI, no analysis dispatch/resources/readbacks, shadow-free PSOs, history and legacy isolation")


if __name__ == "__main__":
    test_superior()
