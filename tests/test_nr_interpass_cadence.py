"""Guard scheduling integration: skipped transitions are ordinary inputs, never failures."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_cadence():
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    policy = (ROOT / 'OptiScaler/dlssnr/DlssNr_InterPassPolicy.h').read_text(encoding='utf-8')
    assert 'scale < 0.90f' in policy and 'scale >= 0.80f ? 2u : 1u' in policy
    assert 'return passes > stride ? stride : 0u;' in policy
    assert 'pass + 1 < passes' in policy  # never correct after the final model pass
    select = run[run.index('const uint32_t configuredInterPassMode'):run.index('// Cache only dimension')]
    assert 'Cadence(configuredInterPassMode, workScale, effectivePasses, spatial)' in select
    assert 'if (interPassCadence == 0u) interPassPath = DlssNr::InterPass::Path::Off;' in select
    assert 'nr.interPassCadence != interPassCadence' in select and 'nr.reset = true;' in select
    assert 'configuredInterPassMode == 3u && interPassCadence != 0u' in select
    loop = run[run.index('for (unsigned int pass = 0; pass < effectivePasses'):run.index('ngxTime->End(cmdList);')]
    assert 'const bool interPassDue = DlssNr::InterPass::Due(pass, effectivePasses, interPassCadence);' in loop
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
    assert 'BenchmarkReportInterPassSchedule(interPassCadence, interPassCorrections)' in run
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    assert 'benchmarkCorrections != expectedCorrections' in status
    assert 'interpass_stride,interpass_corrections' in status
    menu = (ROOT / 'OptiScaler/dlssnr/DlssNr_MenuControls.cpp').read_text(encoding='utf-8')
    assert 'InterPass::Cadence(config->DlssNrInterPassReconstruction.value_or_default(),' in menu
    encode = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp').read_text(encoding='utf-8')
    assert 'PendingPasses(effectivePasses, interPassCoveredPasses)' in encode
    assert 'PendingPasses(pass + 1u,' in run
    profiles = (ROOT / 'OptiScaler/dlssnr/PassProfiles.h').read_text(encoding='utf-8')
    grouped = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForInterPass'):]
    assert grouped.index('GuidedResidualGainForScale(gains.high') < grouped.index('CompoundGuidedHigh(gains.high, coveredPasses,')
    assert 'coveredPasses > 1u' in grouped and 'DlssNrGuidedResidualCompoundPasses' in grouped
    assert 'CompoundGuidedHigh(gains.low' not in profiles
    assert 'ImGui::BeginDisabled(interPassShapingCadence == 1u);' in menu
    print('Cadence integration PASS: Off/every-two schedule, reset, ordinary fallback, debug input and reports')

if __name__ == '__main__':
    test_cadence()
