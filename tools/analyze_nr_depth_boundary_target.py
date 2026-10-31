"""CPU-only exploration: one global positive gain targeting depth-edge carrier contrast.

A target fitted on one head/water capture is a quality experiment, not a ghosting
classifier or cross-game calibration. No GPU/runtime/configuration writes.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import ndimage as ndi

from analyze_nr_edit_capture import digest, plane
from analyze_nr_edit_ghosting import load_edit
from analyze_nr_head_water_contrast import masks


def coefficients(a, b):
    # Each channel's negative/positive maps are monotone in its signed edit;
    # their differences have the same sign. Therefore absolute contrast is A+g*B.
    negative_a = np.maximum(np.minimum(a, 0), -.5)
    negative_b = np.maximum(np.minimum(b, 0), -.5)
    positive_a, positive_b = np.maximum(a, 0), np.maximum(b, 0)
    offset = np.abs(negative_a-negative_b)
    slope = np.abs(positive_a-positive_b)
    return offset, slope


def statistic(values, kind):
    if kind == 'mean':
        return float(np.mean(values))
    if kind == 'median':
        return float(np.median(values))
    if kind == 'trimmed_mean':
        cut = len(values)//10
        if not cut:
            return float(np.mean(values))
        chosen = np.partition(values, [cut, len(values)-cut-1])[cut:len(values)-cut]
        return float(np.mean(chosen))
    raise ValueError(kind)


def solve(offset, slope, target, kind):
    if len(offset) < 64:
        return None
    if statistic(offset+slope, kind) >= target:
        return 1.
    if statistic(offset+1024*slope, kind) < target:
        return None
    if kind == 'mean':
        return max(1., (target-offset.mean())/slope.mean())
    lo, hi = 1., 1024.
    for _ in range(45):
        mid = (lo+hi)*.5
        if statistic(offset+mid*slope, kind) >= target:
            hi = mid
        else:
            lo = mid
    return float(hi)


def depth_pairs(depth, motion, inverted, radius, minimum_ratio, delta_motion=0., precision_floor=0.):
    """Adjacent depth discontinuity, with colour endpoints moved inside each side.

    No scene-specific ROI or semantic recognition. Ratios use near proximity
    (reversed depth, or 1-depth); they do not assert physical distance ratios.
    Endpoints must remain within 25% of their edge-side depth and retain separation.
    """
    h, w = depth.shape
    if radius < 1 or 2*radius > min(h,w):
        raise ValueError('Radius must fit both image axes')
    p = depth if inverted else 1.-depth
    valid = np.isfinite(p) & (p > 0) & (p <= 1)
    aa, bb, orientations = [], [], []
    for axis in (0, 1):
        shape = [slice(None), slice(None)]
        shape[axis] = slice(radius-1, -radius)
        neighbour = shape.copy()
        neighbour[axis] = slice(radius, -radius+1 if radius > 1 else None)
        a, b = tuple(shape), tuple(neighbour)
        pa, pb = p[a], p[b]
        selected = (valid[a] & valid[b] &
                    (np.maximum(pa,pb) >= minimum_ratio*np.minimum(pa,pb)) &
                    (np.abs(pa-pb) > max(precision_floor, 1e-7)))
        y, x = np.where(selected)
        if axis == 0:
            y += radius-1
        else:
            x += radius-1
        ya, xa, yb, xb = y.copy(), x.copy(), y.copy(), x.copy()
        if axis == 0:
            ya -= radius-1
            yb += radius
        else:
            xa -= radius-1
            xb += radius
        # Reject thin-object pairs that crossed back onto the same background.
        ea, eb = p[ya,xa], p[yb,xb]
        first = p[y,x]
        second = p[y+(axis==0), x+(axis==1)]
        keep = (valid[ya,xa] & valid[yb,xb] &
                (np.abs(ea-first) <= .25*np.maximum(ea,first)) &
                (np.abs(eb-second) <= .25*np.maximum(eb,second)) &
                (np.maximum(ea,eb) >= minimum_ratio*np.minimum(ea,eb)))
        ma, mb = motion[ya,xa], motion[yb,xb]
        keep &= np.isfinite(ma).all(axis=1) & np.isfinite(mb).all(axis=1)
        keep &= np.maximum(np.sum(ma*ma,axis=1), np.sum(mb*mb,axis=1)) > .1**2
        if delta_motion:
            keep &= np.sum((ma-mb)**2,axis=1) > delta_motion**2
        aa.append((ya[keep]*w+xa[keep]).astype(np.int64))
        bb.append((yb[keep]*w+xb[keep]).astype(np.int64))
        orientations.append(np.full(np.count_nonzero(keep), axis, dtype=np.uint8))
    return np.concatenate(aa), np.concatenate(bb), np.concatenate(orientations)


def checks():
    rng = np.random.default_rng(71)
    a, b = rng.uniform(-1, .5, (2048,3)), rng.uniform(-1,.5,(2048,3))
    offset, slope = coefficients(a,b)
    for gain in (1.,2.25,6.,64.):
        expected = np.abs(np.clip(.5+np.minimum(a,0)+gain*np.maximum(a,0),0,65504)-
                          np.clip(.5+np.minimum(b,0)+gain*np.maximum(b,0),0,65504))
        assert np.allclose(expected,offset+gain*slope,rtol=1e-12,atol=1e-12)
    for inverted in (False,True):
        p = np.full((24,32),.01);p[:,:16]=.1
        z = p if inverted else 1-p
        motion=np.zeros((24,32,2));motion[...,0]=5;motion[:,16:,0]=20
        for radius in (1,2,4):
            aa,bb,_=depth_pairs(z,motion,inverted,radius,2)
            assert len(aa)==24
            assert np.all(aa%32==16-radius) and np.all(bb%32==15+radius)
        aa,_,_=depth_pairs(np.full_like(z,.5),motion,inverted,1,2)
        assert len(aa)==0
        z[3:5,15]=np.nan
        aa,bb,_=depth_pairs(z,motion,inverted,1,2)
        assert len(aa)==22
    for kind in ('mean','median','trimmed_mean'):
        offset=np.full(128,.02);slope=np.full(128,.01)
        assert abs(solve(offset,slope,.08,kind)-6)<1e-8
        assert solve(offset,slope,.01,kind)==1
        assert solve(offset,np.zeros(128),.08,kind) is None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('metadata',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    checks()
    m=json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    if (m['full_nr_answer'] or m['edit_saturation']!=1 or m['depth_colour_strength']!=0 or
            m.get('depth_weighted_positive_gain') or m.get('depth_additive_brightness') or m.get('original_guides_active')):
        raise ValueError('Uniform plain edit transport required')
    source_path=args.metadata.with_suffix('.dds')
    depth_path=args.metadata.with_suffix('.depth.dds')
    motion_path=args.metadata.with_suffix('.motion.dds')
    source=load_edit(source_path)
    if not np.isfinite(m['positive_gain']) or m['positive_gain'] <= 0 or not m['depth_inverted']:
        raise ValueError('This seeded reference requires finite positive capture gain and reversed depth')
    raw=np.where(source>0,source.astype(np.float64)/m['positive_gain'],source)
    depth=plane(depth_path,1,41)[...,0]
    motion=plane(motion_path,2,16)
    if raw.shape[:2]!=depth.shape or depth.shape!=motion.shape[:2]:
        raise ValueError('Mismatched grids')
    if .5+1024*np.maximum(raw,0).max()>65504:
        raise ValueError('Affine contrast formula does not model upper clipping')
    character,head,water,_=masks(depth)
    inside=ndi.binary_erosion(character,iterations=2)
    ring=inside & ~ndi.binary_erosion(inside) & head
    cleanwater=ndi.binary_erosion(water,iterations=2)
    distance,nearest=ndi.distance_transform_edt(~cleanwater,return_indices=True)
    yy,xx=np.where(ring & (distance<=32))
    wy,wx=nearest[:,yy,xx]
    a,b=raw[yy,xx],raw[wy,wx]
    offset,slope=coefficients(a,b)
    # Use R/G channel contrast rather than luma: unsigned per-channel contrast is
    # nondecreasing with positive gain, while cross-channel luma can cancel.
    off,slo=offset[:,:2].mean(axis=1),slope[:,:2].mean(axis=1)
    targets={kind:statistic(off+6*slo,kind) for kind in ('mean','median','trimmed_mean')}
    def observe(aa,bb):
        off,slo=coefficients(raw.reshape(-1,3)[aa],raw.reshape(-1,3)[bb])
        off,slo=off[:,:2].mean(axis=1),slo[:,:2].mean(axis=1)
        return dict(pairs=len(aa),controllable_fraction=float(np.mean(slo>0)) if len(aa) else None,
                    offset_mean=float(off.mean()) if len(aa) else None,slope_mean=float(slo.mean()) if len(aa) else None,
                    at_gain={str(g):{kind:statistic(off+g*slo,kind) for kind in targets} for g in (1.,2.25,6.)} if len(aa) else {},
                    suggested_global_gain={kind:solve(off,slo,targets[kind],kind) for kind in targets} if len(aa) else {})
    report=dict(warning='Targets fitted to head/water at user plateau 6x in this same scene, not independent validation or ghost-free labels.',
                sha256={p.name:digest(p) for p in (args.metadata,source_path,depth_path,motion_path)},
                target_stage='Unquantized carrier, absolute per-channel R/G contrast averaged per pair, before private exposure/NGX.',
                reference_head_water_pairs=len(yy),target_at_6x=targets,reference_at_2_25x={kind:statistic(off+2.25*slo,kind) for kind in targets},
                scene_specific_reference_backsolve={kind:solve(off,slo,targets[kind],kind) for kind in targets},cases=[])
    h,w=depth.shape
    def raster_phases(aa,bb,orientation,nx=256,ny=144):
        centers=aa+(radius-1)*np.where(orientation==0,w,1)
        gains=[];counts=[];moments=[]
        for py in range(8):
            for px in range(8):
                xs=((np.arange(nx)*16+px*2+1)*w)//(nx*16)
                ys=((np.arange(ny)*16+py*2+1)*h)//(ny*16)
                chosen=np.isin(centers, (ys[:,None]*w+xs).ravel(), assume_unique=False)
                counts.append(int(chosen.sum()))
                if chosen.sum()<64:
                    gains.append(None)
                    continue
                a,b=raw.reshape(-1,3)[aa[chosen]],raw.reshape(-1,3)[bb[chosen]]
                off,slo=coefficients(a,b)
                off,slo=off[:,:2].mean(axis=1),slo[:,:2].mean(axis=1)
                gain=solve(off,slo,targets['mean'],'mean')
                gains.append(None if gain is None else float(gain))
                moments.append((off.mean(),slo.mean()))
        finite_gains=[x for x in gains if x is not None]
        lo,hi=np.quantile(finite_gains,[.1,.9]) if finite_gains else (None,None)
        normalized=None
        if moments:
            moment=np.mean(moments,axis=0)
            normalized=solve(np.full(64,moment[0]),np.full(64,moment[1]),targets['mean'],'mean')
        return dict(grid=[nx,ny],count_min=min(counts),count_max=max(counts),phases_with_64_pairs=len(moments),
                    phase_gains=gains,p10=lo,p90=hi,mean_of_accepted_phase_moments_gain=normalized)
    for radius in (1,2,4,8):
        for ratio in (1.25,2.,4.):
            for mv_delta in (0.,2.):
                aa,bb,orientation=depth_pairs(depth,motion,m['depth_inverted'],radius,ratio,mv_delta)
                result=observe(aa,bb)
                if ratio==2 and mv_delta==2:
                    result['uniform_raster_sampling']=raster_phases(aa,bb,orientation)
                # Uniformly spaced 1D stratification of actual depth-edge pairs:
                # assesses a cheap compact edge list, NOT a uniform 256x144 raster.
                samples=[]
                if len(aa)>=64:
                    for phase in range(8):
                        idx=np.minimum(((np.arange(256)+(phase+.5)/8)*len(aa)/256).astype(int),len(aa)-1)
                        samples.append(observe(aa[idx],bb[idx])['suggested_global_gain'])
                report['cases'].append(dict(radius=radius,minimum_proximity_ratio=ratio,motion_difference_threshold=mv_delta,
                                            **result,edge_list_256_samples_8_phases=samples))
                print(radius,ratio,mv_delta,len(aa),result['suggested_global_gain'])
    # Brightness-different regions and pure negative pairs need no positive gain.
    # Mark impossible estimates rather than converting a zero slope to maximum gain.
    report['uncontrollable_example'] = dict(offset=.02,slope=0.,target=targets['mean'],suggested_global_gain=None)
    # Repeat the generic selector only inside a fixed head/water diagnostic ROI.
    # This diagnoses global aggregation; it is not an automatic head detector.
    region=(yy.min()-32,xx.min()-64,yy.max()+33,xx.max()+65)
    report['local_vs_rest']=[]
    for radius in (1,2,4,8):
        aa,bb,_=depth_pairs(depth,motion,m['depth_inverted'],radius,2,2)
        ya,xa=aa//w,aa%w;yb,xb=bb//w,bb%w
        y0,x0,y1,x1=region
        local=(ya>=y0)&(ya<y1)&(yb>=y0)&(yb<y1)&(xa>=x0)&(xa<x1)&(xb>=x0)&(xb<x1)
        report['local_vs_rest'].append(dict(radius=radius,local=observe(aa[local],bb[local]),rest=observe(aa[~local],bb[~local])))
    report['quantized_depth_sensitivity']=[]
    for bits in (16,24):
        quantized=np.rint(np.clip(depth,0,1)*((1<<bits)-1))/((1<<bits)-1)
        for radius in (2,4,8):
            aa,bb,_=depth_pairs(quantized,motion,m['depth_inverted'],radius,2,2,2./((1<<bits)-1))
            report['quantized_depth_sensitivity'].append(dict(unorm_bits=bits,radius=radius,**observe(aa,bb)))
    # A fixed edge span in pixels is not scale invariant: test equivalent centre grids
    # and offsets explicitly; this does not reconstruct a newly evaluated NR response.
    report['resolution_note']='Pixel radius and proximity ratio are experimental selection parameters; no claim of cross-resolution or cross-game calibration.'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
