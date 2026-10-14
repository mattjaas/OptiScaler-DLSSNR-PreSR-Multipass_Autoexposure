"""Inter-pass low is neutral or block-averaged; final low uses the proper independent range."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_neutral_low():
    profiles = (ROOT / 'OptiScaler/dlssnr/PassProfiles.h').read_text()
    interpass = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForInterPass'):
                         profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForResolve')]
    resolve = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForResolve'):]
    final_block = profiles[profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForBlock'):
                           profiles.index('inline GuidedResidualGains EffectiveGuidedResidualGainsForInterPass')]
    assert 'DlssNrInterPassNeutralLow' not in final_block
    assert 'gains.low = MeanGuidedLow(firstPass, completedPasses,' in final_block
    assert 'workScale).low;' in final_block  # Mean of each model's scale-adjusted low gain.
    assert 'DlssNrInterPassNeutralLow.value_or_default()' in resolve
    assert 'gains.low = MeanGuidedLow(0u, completedPasses,' in resolve
    assert 'transfer, firstPass, completedPasses, workScale' in resolve  # High still covers only the tail.
    assert 'gains.high =' not in resolve
    assert interpass.index('EffectiveGuidedResidualGainsForBlock(') < interpass.index('gains.low = 1.0f;')
    assert 'gains.high =' not in interpass
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text()
    schedule = run[run.index('const bool interPassNeutralLow'):run.index('// Cache only dimension')]
    assert 'interPassMask != 0u' in schedule and 'DlssNrGuidedResidualShaping.value_or_default() != 0u' in schedule
    assert 'nr.interPassNeutralLow != interPassNeutralLow' in schedule and 'nr.reset = true;' in schedule
    assert schedule.count('nr.interPassNeutralLow = interPassNeutralLow;') == 2
    config = (ROOT / 'OptiScaler/Config.cpp').read_text()
    assert 'DlssNrInterPassNeutralLow.set_from_config(readBool("DlssNr", "InterPassNeutralLow"))' in config
    assert 'GetBoolValue(Instance()->DlssNrInterPassNeutralLow.value_for_config())' in config
    assert 'DlssNrInterPassNeutralLow { false }' in (ROOT / 'OptiScaler/Config.h').read_text()
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text()
    assert 'c.DlssNrInterPassNeutralLow = v.neutralInterPassLow;' in status
    assert 'c.DlssNrInterPassNeutralLow = savedBenchmarkConfig.neutralInterPassLow;' in status
    assert 'interpass low neutral=' in status
    encode = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp').read_text()
    assert 'EffectiveGuidedResidualGainsForResolve(' in encode and 'DlssNrInterPassNeutralLow' not in encode
    menu = (ROOT / 'OptiScaler/dlssnr/DlssNr_MenuControls.cpp').read_text()
    assert 'EffectiveGuidedResidualGainsForResolve(' in menu
    assert 'Keep inter-pass low gain at 1' in (ROOT / 'OptiScaler/dlssnr/DlssNr_MenuControls.cpp').read_text()

if __name__ == '__main__':
    test_neutral_low()
    print('Low mean PASS: inter-pass block average/neutral, final tail/all-model average, independent high range, history/INI')
