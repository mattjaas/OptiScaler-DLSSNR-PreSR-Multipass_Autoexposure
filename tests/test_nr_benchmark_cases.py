"""Guard reference isolation, retained winners and ABBA/report coverage."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def test_benchmark():
    header = (ROOT / 'OptiScaler/dlssnr/DlssNr_BenchmarkCases.h').read_text(encoding='utf-8')
    rows = re.findall(r'\{(\d+), InterPass::Path::(\w+), ([\d.]+)\}', header)
    assert [(int(p), route) for p, route, gain in rows] == [
        (50, 'FusedOptimized'), (59, 'Rgb20'), (65, 'Rgb16')]
    assert all(float(gain) > 0 for p, route, gain in rows)
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    plan = status[status.index('void AddBenchmarkScale'):status.index('void BuildBenchmarkVariants')]
    calls = re.findall(r'add\((\d), ([^,]+), "([^"]+)"\);', plan)
    assert calls[:3] == [('0', '-1', 'No inter-pass'), ('1', '-1', 'Classic reference'),
                         ('2', '-1', 'Fused reference')]
    assert [name for mode, path, name in calls[-4:]] == [
        'Known winner control', 'Inter-pass optimized', 'Inter-pass optimized', 'Known winner control']
    assert [path for mode, path, name in calls[-4:]] == ['int(expected)', '-1', '-1', 'int(expected)']
    assert all(mode == '3' for mode, path, name in calls[3:])
    assert 'Classic optimized control' in plan and 'Fused optimized control' in plan
    assert 'existing.percent == v.percent && std::string(existing.name) == v.name' in status
    assert 'it->samples.insert' in status and '"Mixed routes"' in status
    assert 'window,sample_index,total_nr_gpu_ms' in status
    for column in ('selected_path', 'native_width', 'work_height', 'stddev_ms',
                   'delta_vs_known_winner_ms', 'actual_gain_vs_fused_ms', 'path_matches_control'):
        assert column in status
    restore = status[status.index('void RestoreBenchmarkConfig'):status.index('void AddBenchmarkScale')]
    saved = set(re.findall(r'c\.(DlssNr\w+) =', restore.split('void ApplyBenchmarkVariant')[0]))
    applied = set(re.findall(r'c\.(DlssNr\w+) =', restore.split('void ApplyBenchmarkVariant')[1]))
    assert applied <= saved, f'Benchmark options not restored: {applied - saved}'
    finish = status[status.index('void FinishBenchmark'):status.index('void StartInterPassBenchmark')]
    assert finish.index('benchmarkOverride.store(-1') < finish.index('if (cancelled)')
    assert finish.index('RestoreBenchmarkConfig') < finish.index('if (cancelled)')
    menu = (ROOT / 'OptiScaler/dlssnr/DlssNr_MenuControls.cpp').read_text(encoding='utf-8')
    assert '"Off", "Classic reference", "Fused reference", "Inter-pass optimized"' in menu
    cfg = (ROOT / 'OptiScaler/Config.h').read_text(encoding='utf-8')
    assert re.findall(r'CustomOptional<[^>]+> (DlssNrInterPass\w+)', cfg) == ['DlssNrInterPassReconstruction']
    dx = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
    assert re.findall(r'#include "precompile/(dlssnr_.*)_Shader.h"', dx) == [
        'dlssnr_interpass_rgb16', 'dlssnr_interpass_rgb20', 'dlssnr_residual',
        'dlssnr_finished_color', 'dlssnr_spatial', 'dlssnr_spatial_guides']

if __name__ == '__main__':
    test_benchmark()
    print('Benchmark PASS: references, retained winners, ABBA pooling, route reporting and restoration')
