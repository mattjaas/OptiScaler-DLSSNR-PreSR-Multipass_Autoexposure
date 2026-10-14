"""Guard scheduling integration: skipped transitions are ordinary inputs, never failures."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_cadence():
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    policy = (ROOT / 'OptiScaler/dlssnr/DlssNr_InterPassPolicy.h').read_text(encoding='utf-8')
    schedule = (ROOT / 'OptiScaler/dlssnr/DlssNr_InterPassSchedule.h').read_text(encoding='utf-8')
    assert 'scale <= 0.90f' in policy and 'passes > 1u' in policy
    assert 'scale > 0.84f' not in policy and 'Cadence(' not in policy
    assert 'after > 0.25 && after > before' in schedule
    assert 'std::abs(double(current.high) - current.low)' in schedule
    assert 'pass + 1u >= passes' in schedule  # never after the final model
    assert 'wholeLow ? 0u : covered' in schedule
    assert 'gains[completed - 1u].high' in schedule
    select = run[run.index('const uint32_t configuredInterPassMode'):run.index('// Cache only dimension')]
    assert 'BuildInterPassSchedule(cfg, configuredInterPassMode, workScale, effectivePasses, spatial)' in select
    assert 'if (interPassMask == 0u) interPassPath = DlssNr::InterPass::Path::Off;' in select
    assert 'nr.interPassMask != interPassMask' in select and 'nr.reset = true;' in select
    assert 'configuredInterPassMode == 3u && interPassMask != 0u' in select
    loop = run[run.index('for (unsigned int pass = 0; pass < effectivePasses'):run.index('ngxTime->End(cmdList);')]
    assert 'const bool interPassDue = interPassSchedule.Due(pass, interPassCoveredPasses);' in loop
    assert 'if (interPassDue)\n                reconstructed = BuildInterPassInput(pass, finalAnswer);' in loop
    fallback = loop[loop.index('else\n            {'):loop.index('// Capture the exact pass-2 input')]
    assert 'if (interPassDue)' in fallback and 'if (interPassActive)' not in fallback
    assert fallback.index('if (interPassDue)') < fallback.index('interPassFailed = true;')
    assert 'clamp.Mode = DlssNrMode_ClampProxy;' in fallback and 'passInput = nr.passClamp;' in fallback
    assert '++interPassCorrections;' in loop and 'interPassCoveredPasses = pass + 1u;' in loop
    assert 'copy, passInput,' in loop  # actual pass-2 input even on skipped transitions
    assert 'ID3D12Resource* const originalPassBase = modelInput;' in run
    assert 'low.Transfer = 0u; // ALWAYS current cumulative N - immutable original base.' in run
    assert 'MakeResolveConstants(encoded, effectivePasses, interPassCoveredPasses)' in run
    assert 'BenchmarkReportInterPassSchedule(interPassMask, interPassSuccessfulMask, interPassCorrections)' in run
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    assert 'benchmarkMask != expectedSchedule.expectedMask' in status
    assert 'benchmarkCorrections != InterPass::CorrectionCount(expectedSchedule.expectedMask)' in status
    assert 'interpass_expected_mask,interpass_mask,interpass_corrections' in status
    menu = (ROOT / 'OptiScaler/dlssnr/DlssNr_MenuControls.cpp').read_text(encoding='utf-8')
    assert 'Profiles::BuildInterPassSchedule(*config,' in menu
    encode = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp').read_text(encoding='utf-8')
    assert 'interPassCoveredPasses, effectivePasses,' in encode
    assert 'shapingMode, interPassCoveredPasses,' in run and 'pass + 1u, workScale' in run
    profiles = (ROOT / 'OptiScaler/dlssnr/PassProfiles.h').read_text(encoding='utf-8')
    single = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForPass'):profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForBlock')]
    assert 'const auto settings = BasePassSettings(cfg, pass);' in single
    assert 'GuidedResidualBaseGains(cfg, shapingMode, transfer, settings.style)' in single
    assert 'StructureAdjustedHigh(GuidedResidualGainForScale(gains.high, workScale), settings.localStructure)' in single
    assert 'gains.low = GuidedResidualGainForScale(gains.low, workScale);' in single
    block = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForBlock'):profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForInterPass')]
    assert 'completedPasses - 1u, workScale' in block  # Non-multiplied high uses the actual last model.
    assert 'gains.low = MeanGuidedLow(firstPass, completedPasses,' in block
    assert 'ProductGuidedHigh(firstPass, completedPasses,' in block
    assert 'DlssNrGuidedResidualCompoundPasses' in block
    assert 'transfer, pass,' in block  # each model, not the last style repeated N times
    assert 'std::pow' not in block and 'ProductGuidedHigh' not in single
    assert 'gains.low = ProductGuidedHigh' not in profiles
    assert 'high/mid compounding=' in status and 'configured_structure=' in status
    build = profiles[profiles.index('inline InterPass::GainSchedule BuildInterPassSchedule'):]
    assert 'EffectiveGuidedResidualGainsForPass(cfg, shapingMode, 7u, pass, workScale)' in build
    assert 'DlssNrGuidedResidualCompoundPasses' in build and 'DlssNrInterPassNeutralLow' in build
    assert 'shapingSchedule.expectedCovered' in menu
    assert 'interPassSuccessfulMask |= uint32_t(1) << pass;' in loop
    assert 'Multiply per-pass high/mid within each block' in menu
    print('Gain schedule integration PASS: effective per-model gains, strict growing gap, actual coverage, fallback and reports')

if __name__ == '__main__':
    test_cadence()
