"""Derived quantization bounds for compact disocclusion history; no GPU or image export."""
import argparse
import json
from pathlib import Path
import numpy as np
from analyze_nr_edit_capture import plane


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stems',nargs='+',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();rows=[]
    for stem in args.stems:
        meta=json.loads(Path(str(stem)+'.json').read_text())
        z=np.array(plane(Path(str(stem)+'.depth.dds'),1,41)[...,0])
        mv=np.array(plane(Path(str(stem)+'.motion.dds'),2,16))
        # Audit all input samples; chosen half-raster guides are a subset.
        valid=np.isfinite(mv).all(axis=2)&(np.abs(mv)<65504).all(axis=2)
        half=mv[valid].astype(np.float16).astype(np.float32)
        error=np.abs(half-mv[valid])
        valid_z=np.isfinite(z)&(z>=0)&(z<=1)
        nz=z[valid_z]
        if not meta["depth_inverted"]:nz=1-nz
        nz=nz[nz>0]
        packed=(nz.view(np.uint32)&0xffffff80).view(np.float32)
        rel=np.abs(packed-nz)/nz
        rows.append(dict(sample=stem.name,valid_motion_samples=int(valid.sum()),
                         mv_absolute_error_work_pixels_quantiles=np.quantile(error,[.5,.99,1]).tolist(),
                         depth_relative_error_max=float(rel.max()) if rel.size else None))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(note='Input quantization only; not temporal mask quality or ghost rejection accuracy.',samples=rows),indent=2)+'\n')


if __name__=='__main__':main()
