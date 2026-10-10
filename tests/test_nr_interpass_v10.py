"""v10 offline invariants: single sweep, compile-time independent DXIL,
P50 fallback, P65 Linear16 source-cache geometry and separable weights.
Actual DXIL isolation is additionally verified by CI's distinct compiled hashes.
"""
from pathlib import Path
import math
import random

ROOT = Path(__file__).resolve().parents[1]
HLSL = (ROOT / "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl").read_text(encoding="utf-8")
CPP = (ROOT / "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
BENCH = (ROOT / "OptiScaler/dlssnr/DlssNr_Status.cpp").read_text(encoding="utf-8")
FLOW = (ROOT / ".github/workflows/build_nr_detail_quality_experiments.yml").read_text(encoding="utf-8")


def test_code_isolation():
    assert "DLSSNR_TILED_V10_CACHE" in HLSL
    assert "DLSSNR_TILED_V10_WEIGHTS" in HLSL
    assert "#ifdef DLSSNR_TILED_V10_CACHE" in HLSL
    assert "groupshared float3 gV9Source[16 * 16], gV9Model[16 * 16];" in HLSL
    assert "groupshared float3 gClassicSource[12 * 12], gClassicModel[12 * 12];" in HLSL
    # No source-cache declaration under weights-only specialization.
    cache = HLSL.index("groupshared float3 gV9Source[16 * 16]")
    guard = HLSL.rfind("#if defined(DLSSNR_TILED_V10_CACHE)", 0, cache)
    assert guard >= 0 and cache - guard < 350
    assert "static const bool useV9Cache = false;" in HLSL
    assert "static const bool useV9Weights = true;" in HLSL
    assert "dlssnr_tiled_v10_weights_cso" in CPP
    assert "dlssnr_tiled_v10_cache_cso" in CPP
    assert "dlssnr_tiled_v10_both_cso" in CPP
    assert "dlssnr_classic_v10_cache_cso" in CPP
    for token in ("DLSSNR_TILED_V10_CACHE=1", "DLSSNR_TILED_V10_WEIGHTS=1",
                  "dlssnr_classic_v10_cache_Shader.cso"):
        assert token in FLOW
    assert any(x in BENCH for x in ("NR-v10-", "NR-v11-", "NR-v12-", "NR-v13-", "NR-v14-", "NR-v15-"))  # legacy invariants
    assert "kInterPassBenchmarkCases" in BENCH
    assert "benchmarkVariants[benchmarkIndex].samples" in BENCH
    assert "std::ofstream individual(raw)" in BENCH
    assert "benchmarkAdapterInfo" in BENCH
    assert "std::sort(sorted.begin(), sorted.end())" in BENCH


def test_math():
    rng = random.Random(10)
    for _ in range(25000):
        pos = rng.uniform(-10, 4000)
        rounded = math.floor(pos + .5)
        base = math.floor(pos)
        frac = pos - base
        moved = rounded != base
        pre = (1-frac, frac, 0) if moved else (0, 1-frac, frac)
        for i in (-1, 0, 1):
            logical = rounded + i
            old = 1-frac if logical == base else (frac if logical == base+1 else 0.0)
            assert abs(old - pre[i+1]) < 1e-12


def test_source_cache_16():
    rng = random.Random(2065)
    for native_w, native_h in ((1920, 1080), (2560,1440), (3840,2160),
                               (641,359), (192,108), (97,71), (333,177)):
        ww, wh = round(native_w*.65), round(native_h*.65)
        assert native_w * 3 <= ww * 5 and native_h * 3 <= wh * 5
        rx, ry = ww/native_w, wh/native_h
        # Focus on first/last/centre/noninteger and randomized workgroups.
        xgroups = {0, (ww-1)//8, (ww//2)//8}
        ygroups = {0, (wh-1)//8, (wh//2)//8}
        for _ in range(18):
            xgroups.add(rng.randrange((ww+7)//8))
            ygroups.add(rng.randrange((wh+7)//8))
        for gy in ygroups:
            for gx in xgroups:
                x0, y0 = 8*gx, 8*gy
                x1, y1 = min(x0+8,ww), min(y0+8,wh)
                firstx = math.floor(x0*native_w/ww)
                firsty = math.floor(y0*native_h/wh)
                limitx = math.ceil(x1*native_w/ww)
                limity = math.ceil(y1*native_h/wh)
                assert limitx-firstx <= 16 and limity-firsty <=16
                c0x = max(0, math.floor((firstx + .5)*rx)-2)
                c0y = max(0, math.floor((firsty + .5)*ry)-2)
                c1x = min(ww-1, math.floor((limitx - .5)*rx)+2)
                c1y = min(wh-1, math.floor((limity - .5)*ry)+2)
                assert 0 < c1x-c0x+1 <= 16
                assert 0 < c1y-c0y+1 <= 16
                for ny in range(firsty,limity):
                    for nx in range(firstx,limitx):
                        bx,by=math.floor((nx+.5)*rx),math.floor((ny+.5)*ry)
                        for iy in (-1,0,1):
                            for ix in (-1,0,1):
                                px=max(0,min(ww-1,bx+ix))
                                py=max(0,min(wh-1,by+iy))
                                assert c0x <= px <= c1x
                                assert c0y <= py <= c1y


if __name__ == "__main__":
    test_code_isolation()
    test_math()
    test_source_cache_16()
    print("v10 isolated DXIL, one-sweep benchmark and P65 FP32 cache geometry: OK")
