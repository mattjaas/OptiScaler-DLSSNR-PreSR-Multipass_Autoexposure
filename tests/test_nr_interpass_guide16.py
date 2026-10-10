"""Guard the exact screenshot PSO, focused ABBA and benchmark-only override."""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]

def test_guide16():
    directory = ROOT / 'OptiScaler/shaders/dlssnr/precompile/benchmark_v14'
    data = (directory / 'guide16.cso').read_bytes()
    assert hashlib.sha256(data).hexdigest() == 'f55dadbcf9b48e11d7e623843e28606f6bd0d37ae136d6ceb87084eed8f48c3a'
    assert bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', (directory / 'guide16.h').read_text())) == data
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    build = status[status.index('void BuildBenchmarkVariants'):status.index('void UpdateBenchmarkProgress')]
    guide = build[build.index('InterPassBenchmarkProfile::GuideP65'):build.index('InterPassBenchmarkProfile::RevisionP65')]
    assert re.findall(r'add\((\d), (false|true), "([^"]+)"\);', guide) == [
        ('0', 'false', 'No inter-pass'), ('1', 'false', 'Classic reference'), ('2', 'false', 'Fused reference'),
        ('3', 'false', 'Inter-pass optimized'), ('3', 'true', 'v14 Guide16 (screenshot settings)'),
        ('3', 'true', 'v14 Guide16 (screenshot settings)'), ('3', 'false', 'Inter-pass optimized')]
    assert 'push_back({65,' in guide and 'return;' in guide
    assert '.v14Guide16 = guide16' in guide and '.v14Shaders =' not in guide
    finish = status[status.index('void FinishBenchmark'):status.index('void StartInterPassBenchmark')]
    assert finish.index('benchmarkV14Guide16.store(false') < finish.index('if (cancelled)')
    assert 'NR-v23-p65-guide16-' in finish and 'BenchmarkShaderRevision(v)' in finish
    assert 'v.v14Guide16 ? "v14-guide16"' in status
    assert 'Fused Guide16 RGBA (v14)' in status and 'IsFocusedP65(benchmarkProfile)' in status
    assert 'IsFocusedP65(profile)' in status # full effective metadata also for GuideP65
    dx = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
    dispatch = dx[dx.index('if (DlssNr::BenchmarkUsesV14Guide16())'):dx.index('if (historical)', dx.index('if (DlssNr::BenchmarkUsesV14Guide16())'))]
    assert 'index != 0u || InConstants.ResidualConfidenceSensitivity != 1.0f' in dispatch
    assert '16384u | 16777216u' in dispatch and '~4194304u' in dispatch
    assert 'no fallback timing accepted' in dispatch and 'return false;' in dispatch
    assert 'DispatchCompute(InCmdList, guideConstants, _benchmarkV14Guide16' in dispatch
    assert '_benchmarkV14Standard' not in dispatch # v23 keeps the main shader current
    assert 'if (_benchmarkV14Guide16) _benchmarkV14Guide16->Release();' in dx
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    assert 'BenchmarkUsesV14Guide16() ? 0x200u : 0u' in run
    ui = (ROOT / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
    assert 'Compare Optimized / v14 Guide16 at 65% (CSV)' in ui

if __name__ == '__main__':
    test_guide16()
    print('Guide16 PASS: exact historical bytes, focused ABBA, current main shader, guards and restoration')
