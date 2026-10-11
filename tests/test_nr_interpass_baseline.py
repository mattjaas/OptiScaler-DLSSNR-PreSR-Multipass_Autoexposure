"""Baseline ABBA must keep RGB16 current and subtract Off of the same revision."""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]

def test_baseline():
    data = (ROOT / 'OptiScaler/shaders/dlssnr/precompile/benchmark_v14/standard.cso').read_bytes()
    assert hashlib.sha256(data).hexdigest() == '5599ee0b01fc50b9bea4c2d02c8bfc1ce24287e9867dbc9fa0158db883bab9fc'
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    build = status[status.index('void BuildBenchmarkVariants'):status.index('void UpdateBenchmarkProgress')]
    baseline = build[build.index('InterPassBenchmarkProfile::BaselineP65'):build.index('InterPassBenchmarkProfile::GuideP65')]
    assert re.findall(r'add\((\d), (false|true), "([^"]+)"\);', baseline) == [
        ('0', 'false', 'No inter-pass'), ('0', 'true', 'No inter-pass (v13/v14 standard)'),
        ('0', 'true', 'No inter-pass (v13/v14 standard)'), ('0', 'false', 'No inter-pass'),
        ('1', 'false', 'Classic reference'), ('2', 'false', 'Fused reference'),
        ('3', 'false', 'Inter-pass optimized'), ('3', 'true', 'Optimized (v13/v14 standard; current RGB16)'),
        ('3', 'true', 'Optimized (v13/v14 standard; current RGB16)'), ('3', 'false', 'Inter-pass optimized')]
    assert 'push_back({65,' in baseline and 'return;' in baseline
    assert '.v13V14StandardOnly = historicalStandard' in baseline and '.v14Shaders =' not in baseline
    finish = status[status.index('void FinishBenchmark'):status.index('void StartInterPassBenchmark')]
    assert finish.index('benchmarkV13V14StandardOnly.store(false') < finish.index('if (cancelled)')
    assert 'NR-v24-p65-baseline-' in finish and 'v13-v14-standard' in status
    assert 'v.mode == 0 && !v.v13V14StandardOnly' in finish
    assert 'v.mode == 0 && v.v13V14StandardOnly' in finish
    assert 'v.v13V14StandardOnly ? historicalOff.at(v.percent) : off.at(v.percent)' in finish
    assert 'delta_vs_matching_off_median_ms,gain_vs_current_same_mode_ms' in finish
    dx = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
    factory = dx[dx.index('bool DlssNr_Dx12::EnsureBenchmarkBaselineStandard'):dx.index('bool DlssNr_Dx12::DispatchPass(')]
    assert '_benchmarkV14Rgb16' not in factory and 'no fallback timing accepted' in factory
    assert 'if (_benchmarkV14Standard) return true;' in factory
    assert dx.count('if (DlssNr::BenchmarkUsesV13V14StandardOnly())') == 2
    aux = dx[dx.index('bool DlssNr_Dx12::DispatchPassAux2'):dx.index('bool DlssNr_Dx12::DispatchCompute')]
    assert 'if (historical)' in aux # archived RGB only for the old v22 control, never standard-only
    assert 'pipeline = _interPassRgbPipeline[index]' in aux
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    assert 'BenchmarkUsesV13V14StandardOnly() ? 0x400u : 0u' in run
    ui = (ROOT / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
    assert 'Compare current / v13-v14 baseline at 65% (CSV)' in ui

if __name__ == '__main__':
    test_baseline()
    print('Baseline PASS: two ABBA schedules, standard-only PSO, matching Off denominator, guards/restoration')
