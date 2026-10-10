"""FP32 RGB/alpha invariance, strict routing, restore and compiled isolation.

Actual compiled DX12 fixture parity is checked separately by nr_gpu/v14_parity.cpp.
These checks do not measure GPU speed or prove temporal game parity.
"""
from pathlib import Path
import random
import struct
import re

ROOT = Path(__file__).resolve().parents[1]

def f32(x):
    return struct.unpack('f',struct.pack('f',x))[0]

def test_rgb_and_guide():
    rng=random.Random(1414)
    for _ in range(10000):
        rgba=[0.0]*4; rgb=[0.0]*3
        for tap in range(rng.randrange(1,10)):
            colour=[f32(rng.uniform(-64,64)) for _ in range(4)]
            weight=f32(rng.random())
            rgba=[f32(a+f32(c*weight)) for a,c in zip(rgba,colour)]
            rgb=[f32(a+f32(c*weight)) for a,c in zip(rgb,colour)]
        assert rgba[:3]==rgb
    # Counterexample to replacing lerp at t=1 with guided: subtraction rounds.
    b=f32(1.0); g=f32(2**-30)
    assert f32(f32(g-b)+b)!=g

def index(compact,rgb,guide):
    combination=int(rgb)+2*int(guide)
    assert not compact or combination>0
    return combination-1 if compact else 3+combination

def eligible(flags,confidence):
    compact=bool(flags&8192); strided=bool(flags&4096)
    rgb=bool(flags&4194304); guide=bool(flags&16777216) and confidence==1.0
    linear20=not compact and bool(flags&8388608)
    old=32768|65536|131072|262144|524288|1048576
    ok=not strided and bool(flags&16384) and not (flags&old) and ((rgb or guide) if compact else linear20)
    return index(compact,rgb,guide) if ok else None

def test_routing():
    assert sorted(index(c,r,g) for c in (False,True) for r in (False,True)
                  for g in (False,True) if not c or r or g)==list(range(7))
    assert eligible(8192|16384|4194304,0.65)==0
    assert eligible(8192|16384|16777216,1.0)==1
    for strength in (0.0,0.65,0.99999994,1.00000012,float('nan')):
        assert eligible(8192|16384|16777216,strength) is None
    assert eligible(16384|8388608,1.0)==3
    assert eligible(16384|8388608|4194304|16777216,1.0)==6
    for conflicting in (4096,32768,65536,131072,262144,524288,1048576):
        assert eligible(8192|16384|4194304|conflicting,1.0) is None
    assert eligible(16384|4194304,1.0) is None  # 20x20 must be explicitly enabled.

def test_integration():
    shader=(ROOT/'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl').read_text()
    helper=shader[shader.index('float3 InterPassGuideBlend'):shader.index('// Hue-preserving gamut')]
    assert helper.index('return lerp(bilinear, guided, saturate(gResidualConfidenceUnused))') < helper.index('#define gResidualConfidenceUnused 1.0')
    assert 'return guided;' not in helper
    assert '#define DLSSNR_TILE_VALUE float3' in shader
    assert 'corrected.a = gOriginal.Load(int3(acx, acy, 0)).a;' in shader
    config=(ROOT/'OptiScaler/Config.h').read_text()
    bench=(ROOT/'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text()
    cpp=(ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
    take=bench[bench.index('BenchmarkConfig TakeBenchmarkConfig'):bench.index('void RestoreBenchmarkConfig')]
    restore=bench[bench.index('void RestoreBenchmarkConfig'):bench.index('void ApplyBenchmarkVariant')]
    apply=bench[bench.index('void ApplyBenchmarkVariant'):bench.index('void AddBenchmark')]
    for name in ('RgbTile','Linear20','GuideOne'):
        field='DlssNrInterPassV14'+name
        assert field+' { false }' in config
        assert field+'.value_or_default()' in take
        assert field+' = v.v14'+name in restore and field+' = v.v14'+name in apply
    assert 'std::map<int, double> baselines' in bench and 'baselines.at(v.percent)' in bench
    assert 'kInterPassBenchmarkCases' in bench
    assert 'InConstants.ResidualConfidenceSensitivity == 1.0f' in cpp
    assert 'if (v13 && pipeline == _pipelineState)' in cpp
    assert 'for (auto*& state : _tiledV14PipelineState)' in cpp
    compile_script=(ROOT/'tools/compile_nr_v14.ps1').read_text()
    for name in ('rgb16','guide16','both16','weights20','rgb20','guide20','both20'):
        assert 'dlssnr_tiled_v14_'+name+'_cso' in cpp and 'Name="'+name+'"' in compile_script
    # Generated outputs are optional locally, mandatory in the CI compile step.
    directory=ROOT/'OptiScaler/shaders/dlssnr/precompile'
    for name,floats in (('rgb16',768),('both16',768),('rgb20',1200),('both20',1200)):
        asm=directory/('dlssnr_tiled_v14_'+name+'.asm')
        if asm.exists():
            lds=re.findall(r'^.*addrspace\(3\) global.*$',asm.read_text(),re.M)
            assert len(lds)==1 and '['+str(floats)+' x float]' in lds[0]

if __name__=='__main__':
    test_rgb_and_guide(); test_routing(); test_integration()
    print('v14 FP32 RGB/order, alpha preservation, strict guide eligibility, routing and state restoration: PASS')
