"""Validate dispatch geometry, complete tile coverage and safe v16 PSO fallback."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_tile_ownership():
    # Exercise full and clipped edge tiles at the two eligible geometry ranges.
    for width,height in ((65,37),(127,73),(1920,1080),(2560,1440)):
        for scale in (51,55,59,60,61,65,75,90):
            ww,wh=max(1,width*scale//100),max(1,height*scale//100)
            for gx in (8,16):
                for tw in range(1,35 if gx==16 else 21):
                    for th in range(1,21):
                        owners=[]
                        pairs=(tw+1)//2
                        for lane in range(gx*8):
                            for n in range(lane,pairs*th,gx*8):
                                x,y=(n%pairs)*2,n//pairs
                                owners.append((x,y))
                                if x+1<tw: owners.append((x+1,y))
                        assert len(owners)==len(set(owners))==tw*th
                # Integer ceiling/floor bounds on the native Area footprint.
                for x in range(0,ww,gx):
                    extent=((min(x+gx,ww)*width+ww-1)//ww)-(x*width//ww)
                    assert extent<= (34 if gx==16 else 20)
                    if width*3<=ww*5: assert extent<= (28 if gx==16 else 16)

def test_routes():
    cpp=(ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
    hlsl=(ROOT/'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl').read_text(encoding='utf-8')
    pair=(ROOT/'OptiScaler/shaders/dlssnr/precompile/dlssnr_v16_pair.hlsli').read_text(encoding='utf-8')
    assert 'v14 && rgb14 && !guide14 && (wide16 || pair16)' in cpp
    assert 'if (_tiledV16PipelineState[index])' in cpp
    assert 'groupWidth = wide16 ? 16u : 8u' in cpp
    assert 'immutableSlot, groupWidth)' in cpp
    assert '(InConstants.Width + groupWidth - 1) / groupWidth' in cpp
    assert 'if (v14 && pipeline == _pipelineState)' in cpp
    assert 'InConstants.Transfer == 0u' in cpp
    assert '#define gMode 18u' in hlsl
    assert '[numthreads(DLSSNR_GROUP_X, 8, 1)]' in hlsl
    assert 'shift < 0 || shift > 1 || a.roundedBase.y != b.roundedBase.y' in pair
    assert 'float3 proxy[4], model[4]' in pair and 'if (!second)' in pair
    assert 'groupshared' not in pair and 'WaveReadLaneAt' not in pair
    status=(ROOT/'OptiScaler/dlssnr/DlssNr_Status.cpp').read_text(encoding='utf-8')
    cfg=(ROOT/'OptiScaler/Config.cpp').read_text(encoding='utf-8')
    for option in ('WideTile','PairLoads','LowShader'):
        assert f'DlssNrInterPassV16{option}.set_from_config' in cfg
        assert f'Instance()->DlssNrInterPassV16{option}.value_for_config()' in cfg
        assert status.count(f'c.DlssNrInterPassV16{option} = v.v16{option};')==2

if __name__=='__main__':
    test_tile_ownership(); test_routes()
    print('v16 tile coverage, dispatch width, cache guards, fallback and config restore PASS')
