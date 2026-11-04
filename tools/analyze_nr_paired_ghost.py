"""Audit matched raw/after private-DLSS captures; derived statistics, never ghost ground truth.

Requires NumPy. Optional --guided-dir reads outputs from the actual production shader:
  private_exposure_quality.exe --guided-ghost-capture <capture stem>
No GPU work, images or absolute input paths are published by this script.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from analyze_nr_edit_capture import plane


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stems', nargs='+', type=Path)
    parser.add_argument('--guided-dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    records = []
    for stem in args.stems:
        planes = {suffix: plane(Path(str(stem) + suffix), channels, dxgi)
                  for suffix, channels, dxgi in (('.raw.dds', 4, 2), ('.after.dds', 4, 2),
                                                 ('.depth.dds', 1, 41), ('.motion.dds', 2, 16))}
        shapes = {p.shape[:2] for p in planes.values()}
        if len(shapes) != 1:
            raise ValueError(f'Unmatched dimensions: {stem.name}')
        metadata = json.loads(Path(str(stem) + '.json').read_text())
        delta = np.abs(planes['.after.dds'][..., :3] - planes['.raw.dds'][..., :3])
        record = dict(sample=stem.name, width=delta.shape[1], height=delta.shape[0],
                      capture_frame=metadata.get('frame'),
                      rgb_absolute_difference_mean=delta.mean(axis=(0, 1), dtype=np.float64).tolist(),
                      max_channel_difference_quantiles=np.quantile(delta.max(axis=2), [.5, .9, .99]).tolist(),
                      sha256={suffix: hashlib.sha256(Path(str(stem) + suffix).read_bytes()).hexdigest()
                              for suffix in (*planes, '.json')})
        if args.guided_dir:
            path = args.guided_dir / (stem.name + '.guided.dds')
            output = plane(path, 4, 2)
            if output.shape != planes['.raw.dds'].shape:
                raise ValueError(f'Unmatched guided output: {stem.name}')
            mask = output[..., 3]
            if not np.isfinite(output).all() or (mask < 0).any() or (mask > 1).any():
                raise ValueError(f'Invalid guided output: {stem.name}')
            record['guided_mask_mean'] = float(mask.mean(dtype=np.float64))
            record['guided_mask_above_half_percent'] = float(np.mean(mask > .5) * 100)
        records.append(record)
    report = dict(note='RGB disagreement and mask coverage are not labelled ghost precision/recall. '
                       'Separated captures cannot validate temporal visibility history.', samples=records)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
