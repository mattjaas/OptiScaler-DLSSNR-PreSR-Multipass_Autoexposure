"""v12 opt-in P65 Linear16 source-addressing parity checks.

The v10 shader is the untouched reference. v12 only changes construction of
the clamped 3x3 Load coordinates, not bilateral exp2, range/shadow weighting,
floating-point accumulation order, or native Area resolve.
"""
from pathlib import Path
from math import floor, exp2
import random

ROOT = Path(__file__).resolve().parents[1]
HLSL = (ROOT / "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl").read_text(encoding="utf-8")
DX12 = (ROOT / "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
RUN = (ROOT / "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "OptiScaler/Config.h").read_text(encoding="utf-8")
BENCH = (ROOT / "OptiScaler/dlssnr/DlssNr_Status.cpp").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build_nr_detail_quality_experiments.yml").read_text(encoding="utf-8")


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def test_compilation_isolation():
    for flag in ("DLSSNR_TILED_V12_INTERIOR", "DLSSNR_TILED_V12_AXES"):
        assert f"#ifdef {flag}" in HLSL
    assert "v12Interior ? logical" in HLSL
    assert "v12SampleX[ox + 1]" in HLSL
    assert "v12SampleY[oy + 1]" in HLSL
    assert "float2(coord) - sourcePos" in HLSL
    assert "const float w = exp2(" in HLSL
    assert "262144u" in DX12 and "524288u" in DX12
    assert "DlssNrInterPassV12Interior { false }" in CONFIG
    assert "DlssNrInterPassV12Axes { false }" in CONFIG
    assert "DlssNrInterPassV12Interior.value_or_default()" in RUN
    assert "DlssNrInterPassV12Axes.value_or_default()" in RUN
    for shader in ("v12_interior", "v12_axes", "v12_both"):
        assert f"dlssnr_tiled_{shader}_cso" in DX12
        assert f"dlssnr_tiled_{shader}_Shader.cso" in WORKFLOW
    assert 'name << "NR-v15-"' in BENCH
    assert "c.DlssNrInterPassV12Interior = v.v12Interior;" in BENCH
    assert "c.DlssNrInterPassV12Axes = v.v12Axes;" in BENCH
    assert "kInterPassBenchmarkCases" in BENCH
    assert "benchmarkWarm < benchmarkWarmup" in BENCH


def test_all_addressing_modes_are_equivalent():
    rng = random.Random(1212)
    for _ in range(100000):
        sw = rng.randrange(1, 4097)
        sh = rng.randrange(1, 2305)
        # Includes interior, borders, OOB and fractional source positions.
        cx = rng.choice((0, 1, sw-2, sw-1, sw//2, rng.randrange(-3, sw+4)))
        cy = rng.choice((0, 1, sh-2, sh-1, sh//2, rng.randrange(-3, sh+4)))
        cx, cy = int(cx), int(cy)
        source_x = cx + rng.uniform(-0.49, .49)
        source_y = cy + rng.uniform(-0.49, .49)
        rounded_x = floor(source_x + .5)
        rounded_y = floor(source_y + .5)
        interior = 0 < rounded_x < sw-1 and 0 < rounded_y < sh-1
        raw_x = [rounded_x-1, rounded_x, rounded_x+1]
        raw_y = [rounded_y-1, rounded_y, rounded_y+1]
        axes_x = raw_x if interior else [clamp(x, 0, sw-1) for x in raw_x]
        axes_y = raw_y if interior else [clamp(y, 0, sh-1) for y in raw_y]
        axes_no_guard_x = [clamp(x, 0, sw-1) for x in raw_x]
        axes_no_guard_y = [clamp(y, 0, sh-1) for y in raw_y]
        for iy in range(3):
            for ix in range(3):
                logical = (raw_x[ix], raw_y[iy])
                original = (clamp(logical[0], 0, sw-1),
                            clamp(logical[1], 0, sh-1))
                guard = logical if interior else original
                axes = (axes_no_guard_x[ix], axes_no_guard_y[iy])
                combined = (axes_x[ix], axes_y[iy])
                assert original == guard == axes == combined
                # Distance and bilateral Gaussian are based on clamped position.
                baseline_dist = ((original[0] - source_x)**2 +
                                 (original[1] - source_y)**2)
                for p in (guard, axes, combined):
                    d = (p[0] - source_x)**2 + (p[1] - source_y)**2
                    assert d == baseline_dist
                    assert exp2(-.73 * d) == exp2(-.73 * baseline_dist)


def test_real_render_scales_and_boundary_groups():
    for nw, nh in ((3840,2160),(2560,1440),(1920,1080),(1601,901),(641,359)):
        for fraction in (.50, .65, .67, .75, .90):
            ww, wh = round(nw*fraction),round(nh*fraction)
            for p100y in (0, 1, nh//2, nh-2, nh-1):
                for p100x in (0, 1, nw//2, nw-2, nw-1):
                    posx=(p100x+.5)*ww/nw-.5
                    posy=(p100y+.5)*wh/nh-.5
                    bx,by=floor(posx+.5),floor(posy+.5)
                    interior=0 < bx < ww-1 and 0 < by < wh-1
                    for oy in (-1,0,1):
                        for ox in (-1,0,1):
                            ref=(clamp(bx+ox,0,ww-1),clamp(by+oy,0,wh-1))
                            guarded=(bx+ox,by+oy) if interior else ref
                            assert ref==guarded


if __name__ == "__main__":
    test_compilation_isolation()
    test_all_addressing_modes_are_equivalent()
    test_real_render_scales_and_boundary_groups()
    print("v12 boundary/interior/axes/combined address and bilateral spatial parity: OK")
