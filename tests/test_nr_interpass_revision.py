"""Verify historical byte identity and containment of the focused P65 control."""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]

def test_revision():
    directory = ROOT / 'OptiScaler/shaders/dlssnr/precompile/benchmark_v14'
    for stem, length, digest in [
        ('standard', 318704, '5599ee0b01fc50b9bea4c2d02c8bfc1ce24287e9867dbc9fa0158db883bab9fc'),
        ('rgb16', 47580, 'e0b673de1c16be5c1100ccdbac751f601bc8a2d275a70596c9101164f7cbbd3f'),
    ]:
        data = (directory / (stem + '.cso')).read_bytes()
        assert len(data) == length and hashlib.sha256(data).hexdigest() == digest
        header = (directory / (stem + '.h')).read_text(encoding='utf-8')
        assert bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', header)) == data

    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    build = status[status.index('void BuildBenchmarkVariants'):status.index('void UpdateBenchmarkProgress')]
    revision = build[build.index('InterPassBenchmarkProfile::RevisionP65'):build.index('InterPassBenchmarkProfile::FastQuality')]
    assert re.findall(r'add\((\d), (false|true), "([^"]+)"\);', revision) == [
        ('0', 'false', 'No inter-pass'), ('1', 'false', 'Classic reference'), ('2', 'false', 'Fused reference'),
        ('3', 'false', 'Inter-pass optimized'), ('3', 'true', 'Optimized with v14 shaders'),
        ('3', 'true', 'Optimized with v14 shaders'), ('3', 'false', 'Inter-pass optimized')]
    assert 'push_back({65,' in revision and 'return;' in revision
    finish = status[status.index('void FinishBenchmark'):status.index('void StartInterPassBenchmark')]
    assert finish.index('benchmarkV14Shaders.store(false') < finish.index('if (cancelled)')
    assert 'v.v14Shaders ? "v14" : "current"' in finish and 'NR-v22-p65-revision-' in finish
    assert 'benchmarkNativeW != 3840u' in status and 'benchmarkWorkH != 1404u' in status
    assert 'variant.v14Shaders ? "Fused RGB16 (v14 shaders)" : "Fused RGB16"' in status
    query = status[status.index('bool BenchmarkUsesV14Shaders'):status.index('void AbortInterPassBenchmark')]
    assert 'benchmarkActive.load' in query and 'benchmarkV14Shaders.load' in query

    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    assert 'BenchmarkUsesV14Shaders() ? 0x100u : 0u' in run
    assert 'nr.interPassMode != interPassHistoryKey' in run
    dx = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
    assert 'if (_benchmarkV14Standard) _benchmarkV14Standard->Release();' in dx
    assert 'if (_benchmarkV14Rgb16) _benchmarkV14Rgb16->Release();' in dx
    assert 'Archived v14 PSO creation failed; no historical timing accepted.' in dx
    assert 'DispatchCompute(InCmdList, InConstants, _benchmarkV14Rgb16' in dx

if __name__ == '__main__':
    test_revision()
    print('P65 revision PASS: archived DXIL identity, fixed ABBA, scoped override, restoration and geometry')
