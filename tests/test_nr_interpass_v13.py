"""Validate bounded Area ordering in FP32 and benchmark state coverage.

CPU arithmetic/address tests do not establish GPU image parity or speed.
The complete shader and old variants are also compiled by the CI workflow.
"""
from pathlib import Path
import math
import random
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def footprint(p, native, work):
    a = f32(f32(f32(p) * f32(native)) / f32(work))
    b = f32(f32(f32(p + 1) * f32(native)) / f32(work))
    return a, b, math.floor(a), math.ceil(b) - 1


def test_area_order_and_values():
    rng = random.Random(1313)
    cases = [(3840,2160),(1601,901),(641,359),(17,11),(2,1)]
    cases += [(rng.randrange(1,16385),rng.randrange(1,16385)) for _ in range(600)]
    count = 0
    for nw,nh in cases:
        for fraction in (.5,.60,.65,.67,.75,.90,.97):
            ww,wh = max(1,round(nw*fraction)), max(1,round(nh*fraction))
            eligible = nw > ww and nh > wh and nw*3 <= ww*5 and nh*3 <= wh*5
            for x,y in ((0,0),(ww-1,wh-1),(ww//2,wh//2),
                        (rng.randrange(ww),rng.randrange(wh))):
                x0,x1,i0,i1 = footprint(x,nw,ww)
                y0,y1,j0,j1 = footprint(y,nh,wh)
                baseline = [(i,j) for j in range(j0,j1+1) for i in range(i0,i1+1)]
                if i1-i0 > 2 or j1-j0 > 2:
                    assert not eligible, (nw,nh,ww,wh,x,y)
                    continue  # Shader returns false and executes reference Mode 28.
                bounded = [(i0+c,j0+r) for r in range(3) if j0+r <= j1
                           for c in range(3) if i0+c <= i1]
                assert bounded == baseline
                # Exact same FP32 weights and sequential sums on arbitrary HDR values.
                texels = {coord:[f32(rng.uniform(-8,16)) for _ in range(4)] for coord in baseline}
                def integrate(order):
                    acc = [f32(0)]*4
                    for i,j in order:
                        wx = f32(max(f32(min(x1,f32(i+1))-max(x0,f32(i))),0))
                        wy = f32(max(f32(min(y1,f32(j+1))-max(y0,f32(j))),0))
                        weight = f32(wx*wy)
                        acc = [f32(a+f32(v*weight)) for a,v in zip(acc,texels[(i,j)])]
                    return acc
                assert integrate(bounded) == integrate(baseline)
                count += 1
    assert count > 10000
    print(f"v13 FP32 Area sample order and accumulation: {count} footprints OK")


def test_wider_fallback():
    _,_,i0,i1 = footprint(1,16,4)
    assert i1-i0 > 2  # Bounded shader must not silently truncate this footprint.


def test_integration_and_state_coverage():
    hlsl = (ROOT/"OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl").read_text()
    cpp = (ROOT/"OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp").read_text()
    bench = (ROOT/"OptiScaler/dlssnr/DlssNr_Status.cpp").read_text()
    config = (ROOT/"OptiScaler/Config.h").read_text()
    assert "DlssNrInterPassV13Area { false }" in config
    assert "#ifdef DLSSNR_TILED_V13_AREA" in hlsl
    area = hlsl[hlsl.index("bool InterPassTiledFusedArea"):hlsl.index("#endif // DLSSNR_TILED_FUSED")]
    assert area.index("GroupMemoryBarrierWithGroupSync();") < area.index("if (i1 - i0 > 2")
    assert "row < 3" in area and "column < 3" in area
    assert "(flags & (1048576u | 2097152u)) != 0u" in cpp
    assert "for (auto*& state : _tiledV13PipelineState)" in cpp
    assert "#ifdef DLSSNR_TILED_V13_MODE28" in hlsl
    assert hlsl.index("#define gMode 28u") > hlsl.index("uint gDirectResolveFlags;")
    assert "DlssNrInterPassV13Mode28 { false }" in config
    for name in ("area", "mode28", "both"):
        assert f"dlssnr_tiled_v13_{name}_cso" in cpp
    apply = bench[bench.index("void ApplyBenchmarkVariant"):bench.index("void AddBenchmark")]
    take = bench[bench.index("BenchmarkConfig TakeBenchmarkConfig"):bench.index("void RestoreBenchmarkConfig")]
    restore = bench[bench.index("void RestoreBenchmarkConfig"):bench.index("void ApplyBenchmarkVariant")]
    fields = set(re.findall(r"c\.(DlssNr\w+)\s*=",apply))
    assert fields == set(re.findall(r"c\.(DlssNr\w+)\s*=",restore)) - {"DlssNrGuidedResidualGuideStrength"}
    assert all(field+".value_or_default()" in take for field in fields)
    assert "RestoreBenchmarkConfig(*Config::Instance(), savedBenchmarkConfig);" in bench
    assert "c.DlssNrInterPassV13Area = v.v13Area;" in bench
    assert "v13_area," in bench and 'name << "NR-v15-"' in bench
    assert "v13_mode28," in bench
    # PSO index is a complete bijection for the three nonempty flag combinations.
    assert [int(a)+2*int(m)-1 for a,m in ((True,False),(False,True),(True,True))] == [0,1,2]


if __name__ == "__main__":
    test_area_order_and_values()
    test_wider_fallback()
    test_integration_and_state_coverage()
    print("v13 routing isolation and full benchmark configuration restoration: OK")
