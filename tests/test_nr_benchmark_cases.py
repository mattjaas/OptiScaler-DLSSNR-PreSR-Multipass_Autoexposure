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
    plan = status[status.index('void AddBenchmarkScale'):status.index('void AddLowBenchmarkScale')]
    calls = re.findall(r'add\((\d), ([^,]+), "([^"]+)"\);', plan)
    assert calls[:3] == [('0', '-1', 'No inter-pass'), ('1', '-1', 'Classic reference'),
                         ('2', '-1', 'Fused reference')]
    assert [name for mode, path, name in calls[-4:]] == [
        'Expected path control', 'Inter-pass optimized', 'Inter-pass optimized', 'Expected path control']
    assert [path for mode, path, name in calls[-4:]] == ['int(expected)', '-1', '-1', 'int(expected)']
    assert all(mode == '3' for mode, path, name in calls[3:])
    assert 'Classic optimized control' in plan and 'Fused optimized control' in plan
    assert 'existing.percent == v.percent && std::string(existing.name) == v.name' in status
    assert 'it->samples.insert' in status and '"Mixed routes"' in status
    assert 'window,sample_index,total_nr_gpu_ms' in status
    for column in ('selected_path', 'native_width', 'work_height', 'stddev_ms',
                   'delta_vs_expected_path_ms', 'actual_gain_vs_fused_ms', 'path_matches_control',
                   'window_medians_ms', 'window_median_spread_ms'):
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
    low = status[status.index('void AddLowBenchmarkScale'):status.index('void UpdateBenchmarkProgress')]
    assert 'for (const auto path : kInterPassLowBenchmarkOrder)' in low
    assert 'RGB20 guarded candidate' in low and 'Fused optimized candidate' in low
    assert 'Expected path control' in low and 'Inter-pass optimized' in low
    assert 'quiet_NaN()' in low  # No fabricated historical gain below 50.
    assert 'if (current < 50) AddLowBenchmarkScale(current)' in low
    assert 'benchmarkProfile = profile;' in status
    build = status[status.index('void BuildBenchmarkVariants'):status.index('void UpdateBenchmarkProgress')]
    boundary = build[build.index('InterPassBenchmarkProfile::Boundary'):build.index('InterPassBenchmarkProfile::Below50')]
    assert 'for (const int scale : kInterPassBoundaryBenchmarkScales) AddLowBenchmarkScale(scale);' in boundary
    assert 'return;' in boundary and 'AddLowBenchmarkScale(current)' not in boundary
    assert 'kInterPassBoundaryBenchmarkScales {41, 42}' in header
    assert 'NR-v19-boundary-' in status and 'exactly P41/P42; current scale is not appended' in status
    assert 'InterPassBenchmarkProfile profile = InterPassBenchmarkProfile::Regular' in (
        ROOT / 'OptiScaler/dlssnr/DlssNr_Benchmark.h').read_text(encoding='utf-8')
    ui = (ROOT / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
    assert 'Compare inter-pass below 50% (CSV)' in ui
    assert 'DlssNr::InterPassBenchmarkProfile::Below50' in ui
    assert 'Compare inter-pass 41% / 42% (CSV)' in ui
    assert 'DlssNr::InterPassBenchmarkProfile::Boundary' in ui
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text(encoding='utf-8')
    guard = run[run.index('const bool eligibleTiledDims'):run.index('if (workingScaleTiled && eligibleTiledDims')]
    assert '2u * modelWidth' not in guard and '2u * modelHeight' not in guard

if __name__ == '__main__':
    test_benchmark()
    print('Benchmark PASS: references, regular ABBA, low-scale symmetric candidates, route reporting and restoration')
