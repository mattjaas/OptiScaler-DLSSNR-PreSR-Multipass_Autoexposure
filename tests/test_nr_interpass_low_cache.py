"""Check shared Low36 sample geometry, edge coverage and opt-in integration."""
from pathlib import Path
import math
import re

ROOT = Path(__file__).resolve().parents[1]

def test_geometry():
    weights = [11, 15, 17, 17, 15, 11]
    assert sum(weights) ** 2 == 7396
    for width, height in [(1, 1), (17, 11), (127, 73), (1920, 1080), (2266, 1274), (2496, 1404)]:
        low_w, low_h = math.ceil(width / 8), math.ceil(height / 8)
        for size, low_size in [(width, low_w), (height, low_h)]:
            footprint = size / low_size
            for output in {0, low_size // 2, low_size - 1}:
                group, local = divmod(output, 8)
                for tap in range(6):
                    pooled = local * 4 + tap
                    assert 0 <= pooled < 34
                    original = (output + .5) * footprint + (tap - 2.5) * .25 * footprint
                    cached = (group * 8 + .5) * footprint + (pooled - 2.5) * .25 * footprint
                    clamp = lambda v: min(max(v, .5), size - .5)
                    assert abs(clamp(original) - clamp(cached)) < 1e-12
        # All lanes cooperatively initialize every cached value, including partial output groups.
        visits = [0] * (34 * 34)
        for lane in range(64):
            for index in range(lane, len(visits), 64):
                visits[index] += 1
        assert all(count == 1 for count in visits)

def test_integration():
    shader = (ROOT / 'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl').read_text()
    entry = shader[shader.index('void CSMain'):]
    assert entry.index('CacheLow36(groupId.xy, groupThreadId.xy);') < entry.index('if (id.x >= gWidth')
    cache = shader[shader.index('void CacheLow36'):shader.index('[numthreads(8, 8, 1)]')]
    assert 'SanitizeFinite3(residual, 0.0)' in cache
    assert 'GroupMemoryBarrierWithGroupSync();' in cache and 'return;' not in cache
    run = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text()
    assert 'if (interPassPath == DlssNr::InterPass::Path::FastCachedLow36)' in run
    assert 'low.DirectResolveFlags = 32768u;' in run
    assert 'if (guided.GuideWidth != 0u)' in run  # No new work without low shaping.
    status = (ROOT / 'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text()
    build = status[status.index('void BuildBenchmarkVariants'):]
    cached = build[build.index('InterPassBenchmarkProfile::FastCachedLow'):build.index('InterPassBenchmarkProfile::FastQuality')]
    assert re.findall(r'add\((\d), "([^"]+)", (\d)\);', cached) == [
        ('0', 'No inter-pass', '0'), ('1', 'Classic reference', '0'), ('2', 'Fused reference', '0'),
        ('3', 'Inter-pass optimized', '0'), ('4', 'Inter-pass fast', '1'),
        ('8', 'Fast cached low', '1'), ('8', 'Fast cached low', '1'), ('4', 'Inter-pass fast', '1')]
    assert 'kInterPassBenchmarkScales' in cached and 'current' not in cached
    dx = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
    assert '((flags & 32768u) ? 2u : 0u)' in dx
    assert 'standard low PSO fallback' in dx

if __name__ == '__main__':
    test_geometry()
    test_integration()
    print('Low36 shared geometry/edge coverage and opt-in ABBA integration PASS')
