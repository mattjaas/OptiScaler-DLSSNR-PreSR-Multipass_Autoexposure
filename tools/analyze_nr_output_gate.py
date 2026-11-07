"""CPU-only audit of labelled paired edits with an explicitly approximate occluder reference.

Requires NumPy/SciPy; optional previews require Pillow. Never reconstructs true visibility
history from one capture. Published results contain hashes/statistics, not game textures.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy import ndimage

from analyze_nr_edit_capture import plane


def smoothstep(low, high, value):
    if high <= low:
        return (value > low).astype(np.float32)
    x = np.clip((value - low) / (high - low), 0, 1)
    return x * x * (3 - 2 * x)


def evidence(raw, after, reference):
    """Unit positive gain only; independently reported mixture coefficient and gate weight."""
    current = np.clip(.5 + raw, 0, 65504)
    direction = np.clip(.5 + reference, 0, 65504) - current
    residual = .5 + after - current
    d2 = np.sum(direction * direction, axis=-1)
    v2 = np.sum(residual * residual, axis=-1)
    projection = np.sum(residual * direction, axis=-1)
    fraction = projection / np.maximum(d2, 1e-8)
    alignment = np.where(projection > 0, projection ** 2 / np.maximum(d2 * v2, 1e-20), 0)
    weight = smoothstep(.1, .15, np.maximum(fraction, 0)) * smoothstep(.64, .81, alignment)
    weight = np.where(d2 <= 1e-8, 1, weight)
    weight = np.where(np.all(np.abs(residual) <= np.maximum(1 / 1024, np.abs(current) / 1024), axis=-1), 0, weight)
    return fraction, weight


def analyze(stem, seed, waist, preview):
    paths = {suffix: Path(str(stem) + suffix) for suffix in
             ('.raw.dds', '.after.dds', '.depth.dds', '.motion.dds', '.json')}
    m = json.loads(paths['.json'].read_text(encoding='utf-8-sig'))
    if (not m.get('paired_same_evaluation') or m.get('full_nr_answer')
            or m.get('edit_transport_mode', 0) != 0 or m.get('positive_gain') != 1
            or m.get('decoded_after_stage') != 'after_transport_inverse_before_ghost_rejection_and_fade'):
        raise ValueError('Audit restricted to matched signed edits with unit positive-only transport')
    raw = np.asarray(plane(paths['.raw.dds'], 4, 2))[..., :3]
    after = np.asarray(plane(paths['.after.dds'], 4, 2))[..., :3]
    depth = plane(paths['.depth.dds'], 1, 41)[..., 0]
    motion = plane(paths['.motion.dds'], 2, 16)
    h, w = depth.shape
    if raw.shape != after.shape or raw.shape != (h, w, 3) or motion.shape != (h, w, 2):
        raise ValueError('Unmatched planes')
    if not all(np.isfinite(p).all() for p in (raw, after, depth, motion)):
        raise ValueError('Nonfinite capture')
    if not (0 <= seed[0] < w and 0 <= seed[1] < h):
        raise ValueError('Foreground seed outside image')
    # Deliberately capture-specific segmentation, not a runtime depth/metres assumption.
    labels, _ = ndimage.label(depth > .02)
    label = labels[seed[1], seed[0]]
    if label == 0:
        raise ValueError('Seed must select foreground at reversed depth > .02')
    foreground = labels == label
    head = int(np.where(foreground)[0].min())
    if not head < waist < h - 50:
        raise ValueError('Waist must split foreground into nonempty upper and lower regions')
    left = foreground.argmax(axis=1)
    yy, xx = np.indices((h, w))
    distance = left[:, None] - xx
    support = foreground.any(axis=1)[:, None] & ~foreground
    records = []
    for inset in (4, 8, 16):
        reference = raw[np.arange(h), np.minimum(left + inset, w - 1)][:, None, :]
        fraction, weight = evidence(raw, after, reference)
        delta = reference - raw
        brightness = np.abs(delta @ np.array([.2126, .7152, .0722], np.float32))
        strongest = np.abs(delta).max(axis=-1)
        for part, y0, y1 in (('upper', head, waist), ('lower', waist, h - 50)):
            for low, high in ((4, 30), (30, 80), (80, 180)):
                roi = support & (yy >= y0) & (yy < y1) & (distance >= low) & (distance < high)
                if not roi.any():
                    raise ValueError('Empty probe strip')
                records.append(dict(
                    part=part, y=[y0, y1], distance=[low, high], reference_inset=inset,
                    pixels=int(roi.sum()), fraction_median=float(np.median(fraction[roi])),
                    output_gate_mean=float(weight[roi].mean(dtype=np.float64)),
                    brightness_gate_mean=float((1 - smoothstep(.0723, .0823, brightness[roi])).mean()),
                    rgb_gate_mean=float((1 - smoothstep(.0723, .0823, strongest[roi])).mean()),
                    residual_brightness_mean=float(np.abs((after - raw) @
                        np.array([.2126, .7152, .0722], np.float32))[roi].mean())))
        if preview and inset == 8:
            from PIL import Image, ImageDraw
            box = (820, 480, 1500, 960)
            roi = support & (yy >= head) & (yy < waist) & (distance >= 4) & (distance < 180)
            panels = []
            for title, source, mask in (
                    ('Before DLSS: 0.5 + 4 x edit', raw, None),
                    ('After DLSS: 0.5 + 4 x edit', after, None),
                    ('Input luma gate / proxy reference', after, 1 - smoothstep(.0723, .0823, brightness)),
                    ('Output evidence / proxy reference', after, weight)):
                rgb = np.clip(.5 + 4 * source, 0, 1)
                if mask is not None:
                    alpha = np.where(roi, mask * .7, 0)[..., None]
                    rgb = rgb * (1 - alpha) + np.array([.1, 1, .05]) * alpha
                im = Image.fromarray(np.uint8(rgb * 255)).crop(box).resize((510, 360))
                panel = Image.new('RGB', (510, 388), (20, 20, 20))
                panel.paste(im, (0, 28))
                ImageDraw.Draw(panel).text((8, 8), title, fill='white')
                panels.append(panel)
            sheet = Image.new('RGB', (1020, 776))
            for i, im in enumerate(panels):
                sheet.paste(im, ((i % 2) * 510, (i // 2) * 388))
            preview.mkdir(parents=True, exist_ok=True)
            sheet.save(preview / (stem.name + '-output-gate.png'))
    return dict(sample=stem.name, dimensions=[w, h], head=head, waist=waist,
                sha256={s: hashlib.sha256(p.read_bytes()).hexdigest() for s, p in paths.items()}, probes=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stems', type=Path, nargs=2)
    parser.add_argument('--waist', type=int, nargs=2, required=True)
    parser.add_argument('--seed', type=int, nargs=2, default=(1250, 800))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--preview-dir', type=Path)
    args = parser.parse_args()
    report = dict(
        warning='Current silhouette colour at inset 4/8/16 is an APPROXIMATE reference, not saved historical '
                'occluder data. Probe retention is NOT actual mask coverage, precision or recall. '
                'No replay of temporal history, no GPU benchmark, no proof of universal ghost detection.',
        parameters=dict(depth_threshold_reversed=.02, seed=args.seed, output_threshold=.1, output_softness=.05,
                        cosine_squared_interval=[.64, .81], carrier_noise_floor=1 / 1024,
                        input_contrast_threshold=.0723, input_contrast_softness=.01),
        samples=[analyze(stem, args.seed, waist, args.preview_dir) for stem, waist in zip(args.stems, args.waist)])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
