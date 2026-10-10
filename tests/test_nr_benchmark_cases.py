"""Check the actual short-sweep manifest, controls and effective PSO routes."""
from collections import Counter
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def test_short_manifest():
    header=(ROOT/'OptiScaler/dlssnr/DlssNr_BenchmarkCases.h').read_text(encoding='utf-8')
    rows=re.findall(r'\{(50|59|65),\s*([02]),\s*((?:(?:true|false),\s*){10})"([^"]+)"\}',header)
    assert len(rows)==17, 'Unexpected case count or malformed manifest entry'
    cases=[]
    for percent,mode,values,name in rows:
        exact,tiled,compact,weights,mode28,rgb,linear20,wide,pair,low=[v=='true' for v in re.findall('true|false',values)]
        cases.append((int(percent),int(mode),exact,tiled,compact,weights,mode28,rgb,linear20,wide,pair,low,name))
    assert Counter(c[0] for c in cases)=={50:3,59:7,65:7}
    assert len({(c[0],c[-1]) for c in cases})==len(cases)
    for scale in (50,59,65):
        group=[c for c in cases if c[0]==scale]
        off=[c for c in group if c[1]==0]
        reference=[c for c in group if c[-1]=='Fused reference']
        assert len(off)==len(reference)==1
        assert not any(off[0][2:12])
        assert reference[0][1]==2 and not any(reference[0][2:12])
        # Off baseline is measured first, including when the sweep is cancelled.
        assert group[0]==off[0]
    for scale,mode,exact,tiled,compact,weights,mode28,rgb,linear20,wide,pair,low,name in cases:
        if rgb or mode28 or linear20:
            assert exact and tiled and weights and mode==2
        if scale==50:
            assert not tiled and not any((compact,weights,mode28,rgb,linear20,wide,pair,low))
        if scale==59:
            assert not compact  # Actual P59 geometry cannot use Compact16.
            if weights: assert linear20
        if scale==65 and tiled: assert compact and not linear20
    # No slow/redundant variants in the routine sweep, but implementation survives.
    assert not any(word in header for word in ('cache v10','strided','guide-one','quadfill','bounded Area'))
    status=(ROOT/'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    build=status[status.index('void BuildBenchmarkVariants'):status.index('void UpdateBenchmarkProgress')]
    assert 'for (const auto& v : kInterPassBenchmarkCases)' in build
    assert 'v.tiled, false, v.compact' in build and 'v.weights, false, v.name' in build
    assert 'v14GuideOne = true' not in build and 'v13Area = true' not in build
    apply=status[status.index('void ApplyBenchmarkVariant'):status.index('void AddBenchmark')]
    restore=status[status.index('void RestoreBenchmarkConfig'):status.index('void ApplyBenchmarkVariant')]
    for option in ('V9SourceCache','V11Spatial','V11QuadFill','V12Interior','V12Axes','V13Area','V14GuideOne','V16WideTile','V16PairLoads','V16LowShader'):
        assert 'c.DlssNrInterPass'+option+' = v.' in apply
        assert 'c.DlssNrInterPass'+option+' = v.' in restore
    assert 'if (!active) continue;' in build
    assert 'gains.active && gains.high != gains.low' in build
    assert 'baselines.at(v.percent)' in status
    assert 'up to 17 cases (3 P50, 7 P59, 7 P65)' in status
    assert 'name << "NR-v16-"' in status
    menu=(ROOT/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
    assert 'NR v16: short sweep P50/P59/P65, 15-17 cases (CSV)' in menu

if __name__=='__main__':
    test_short_manifest()
    print('Short benchmark: up to 17 cases, P50/P59/P65, baseline coverage and effective PSO routes PASS')
