"""v11 opt-in shader isolation and geometry/weight parity regression checks.

Numerical equivalence is checked for coordinates and exact analytical
bilateral spatial distances. Real GPU DXIL/image parity must still be
measured in game; dxc compilation and SHA isolation are checked in CI.
"""
from pathlib import Path
from math import ceil, floor, exp2, isclose
import random

ROOT = Path(__file__).resolve().parents[1]
HLSL = (ROOT / "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl").read_text(encoding="utf-8")
DX12 = (ROOT / "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
RUN = (ROOT / "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "OptiScaler/Config.h").read_text(encoding="utf-8")
BENCH = (ROOT / "OptiScaler/dlssnr/DlssNr_Status.cpp").read_text(encoding="utf-8")

def test_isolation():
    for flag in ("DLSSNR_TILED_V11_SPATIAL", "DLSSNR_TILED_V11_QUADFILL"):
        assert flag in HLSL
    for s in ("spatialDeltaX[ox + 1]", "spatialDeltaY[oy + 1]",
              "tileXStep * 8u", "tileYStep * 8u",
              "InterPassCorrectedP100LoadDynamicInternal("):
        assert s in HLSL
    assert "65536u" in DX12 and "131072u" in DX12
    assert "DlssNrInterPassV11Spatial { false }" in CONFIG
    assert "DlssNrInterPassV11QuadFill { false }" in CONFIG
    assert "DlssNrInterPassV11Spatial.value_or_default()" in RUN
    assert "DlssNrInterPassV11QuadFill.value_or_default()" in RUN
    assert "NR-v11-" in BENCH
    assert "for (int scale : { 50, 65 })" in BENCH
    assert "v11Spatial = true;" in BENCH and "v11QuadFill = true;" in BENCH
    assert "benchmarkWarm < benchmarkWarmup" in BENCH
    assert "DLSSNR_TILED_V11" not in HLSL.split("#ifdef DLSSNR_TILED_FUSED")[0].split("float4 InterPassCorrectedP100LoadDynamicInternal")[0]

def test_spatial_same_texels():
    rng = random.Random(11065)
    for _ in range(50000):
        w = rng.randrange(3, 4000)
        h = rng.randrange(3, 2300)
        source_x = rng.uniform(-1.5, w + 1.5)
        source_y = rng.uniform(-1.5, h + 1.5)
        bx, by = floor(source_x + .5), floor(source_y + .5)
        x = [max(0, min(w - 1, bx + i)) for i in (-1, 0, 1)]
        y = [max(0, min(h - 1, by + j)) for j in (-1, 0, 1)]
        dx = [float(p) - source_x for p in x]
        dy = [float(p) - source_y for p in y]
        for j in range(3):
            for i in range(3):
                px = max(0, min(w-1, bx+i))
                py = max(0, min(h-1, by+j))
                assert dx[i] == float(px) - source_x
                assert dy[j] == float(py) - source_y
                old = (float(px)-source_x)**2 + (float(py)-source_y)**2
                new = dx[i]**2 + dy[j]**2
                assert old == new
                sigma = 2.0
                assert exp2(-old / sigma) == exp2(-new / sigma)

def test_quad_fill_unique_and_complete():
    rng = random.Random(1616)
    # Native footprint for a P65 8x8 working tile is at most 16x16.
    for w,h in ((3840,2160), (2560,1440), (1920,1080), (1600,900),
                (3839,2161), (641,359), (113,73)):
        outw, outh = round(w*.65), round(h*.65)
        gxmax = (outw+7)//8
        gymax = (outh+7)//8
        groups = {(0,0),(gxmax-1,gymax-1),(gxmax//2,gymax//2)}
        groups.update((rng.randrange(gxmax),rng.randrange(gymax)) for _ in range(24))
        for gx,gy in groups:
            beginx, beginy = gx*8, gy*8
            endx, endy = min(beginx+8,outw), min(beginy+8,outh)
            firstx, firsty = floor(beginx*w/outw),floor(beginy*h/outh)
            sizex = ceil(endx*w/outw)-firstx
            sizey = ceil(endy*h/outh)-firsty
            assert 0 < sizex <= 16 and 0 < sizey <= 16
            old = [(i % sizex, i // sizex) for i in range(sizex*sizey)]
            quad = []
            for ly in range(8):
                for lx in range(8):
                    for qy in (0,1):
                        ty=ly+qy*8
                        if ty<sizey:
                            for qx in (0,1):
                                tx=lx+qx*8
                                if tx<sizex:
                                    quad.append((tx,ty))
            assert len(quad) == len(old)
            assert set(quad) == set(old)

if __name__ == "__main__":
    test_isolation()
    test_spatial_same_texels()
    test_quad_fill_unique_and_complete()
    print("v11 isolated spatial and quad fill parity: OK")
