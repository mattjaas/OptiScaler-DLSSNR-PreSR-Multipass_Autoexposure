"""CPU-only study of NR edit near neighbouring, different-speed motion fields.

No ghosting detector, segmentation ground truth, GPU execution or runtime changes.
The unchanged R/G target is reused only to compare the spatial selection.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_capture import digest, plane, positive_rg, required, score
from analyze_nr_edit_ghosting import load_edit


def boundaries(motion, radius=1, delta=2., ratio=.5, minimum_fast=2.):
    h, w = motion.shape[:2]
    if radius < 1 or radius >= min(h, w):
        raise ValueError("Radius must fit both image axes")
    speed2 = np.sum(motion * motion, axis=-1)
    valid = np.isfinite(motion).all(axis=-1) & (np.abs(motion) < 1e6).all(axis=-1)
    slow = np.zeros((h, w), dtype=bool)
    fast = slow.copy()
    degree = np.zeros((h, w), dtype=np.uint8)
    pairs = []
    for a, b in (((slice(None), slice(None, -radius)), (slice(None), slice(radius, None))),
                 ((slice(None, -radius), slice(None)), (slice(radius, None), slice(None)))):
        difference = motion[a] - motion[b]
        sa, sb = speed2[a], speed2[b]
        selected = (valid[a] & valid[b] & (np.sum(difference * difference, axis=-1) > delta * delta)
                    & (np.minimum(sa, sb) <= ratio * ratio * np.maximum(sa, sb))
                    & (np.maximum(sa, sb) >= minimum_fast * minimum_fast))
        a_slower = sa <= sb
        slow[a] |= selected & a_slower
        slow[b] |= selected & ~a_slower
        fast[a] |= selected & ~a_slower
        fast[b] |= selected & a_slower
        degree[a] += selected
        degree[b] += selected
        pairs.append((a, b, selected))
    return slow, fast, degree, pairs


def example_checks():
    # Uniform panning and a smooth field must not be mistaken for an object edge.
    v = np.zeros((16, 32, 2), dtype=np.float32)
    v[..., 0] = 20
    assert not boundaries(v)[2].any()
    v[..., 0] = np.linspace(5, 20, 32)
    assert not boundaries(v)[2].any()
    # Both fields move by more than 1px: the actual relative boundary still qualifies.
    v[:, :16, 0] = 5
    v[:, 16:, 0] = 20
    slow, fast, degree, _ = boundaries(v)
    assert slow.sum() == fast.sum() == 16 and np.all(slow[:, 15]) and np.all(fast[:, 16])
    assert np.count_nonzero(degree) == 32 and degree.max() == 1
    y, x = np.indices((16, 32))
    v[..., 0] = np.where((x + y) % 2, 5, 20)
    _, _, degree, _ = boundaries(v)
    assert np.all(degree[1:-1, 1:-1] == 4) and degree.min() == 2


def phase_statistics(raw, mask, epsilon, target):
    height, width = mask.shape
    counts, values, accepted = [], [], []
    for py in range(8):
        for px in range(8):
            x = ((np.arange(256) * 16 + px * 2 + 1) * width) // (256 * 16)
            y = ((np.arange(144) * 16 + py * 2 + 1) * height) // (144 * 16)
            sample = raw[y[:, None], x]
            selected = mask[y[:, None], x]
            distribution = positive_rg(sample[selected])
            n = [len(v) for v in distribution]
            counts.append(n)
            gain = required(distribution, epsilon, target)
            values.append(gain)
            if min(n) >= 64 and gain is not None:
                accepted.append(distribution)
    mean_gain = None
    if accepted:
        lo, hi = 0., 1024.
        for _ in range(45):
            mid = (lo + hi) / 2
            mixture_score = np.mean([score(v, epsilon, mid) for v in accepted])
            if mixture_score > target:
                hi = mid
            else:
                lo = mid
        mean_gain = hi
    finite = [v for v in values if v is not None]
    return dict(positive_count_min_rg=np.min(counts, axis=0).tolist(),
                positive_count_max_rg=np.max(counts, axis=0).tolist(),
                phases_with_64_positives_per_channel=len(accepted),
                raw_gain_min=min(finite) if finite else None, raw_gain_max=max(finite) if finite else None,
                normalized_mean_of_accepted_phases_gain=mean_gain, phase_raw_gains=values)


def region(raw, mask, epsilon, target, shown, plateau, phases=False):
    distribution = positive_rg(raw[mask])
    row = dict(pixels=int(mask.sum()), pixel_fraction=float(mask.mean()),
               positive_count_rg=[len(v) for v in distribution],
               raw_suggested_gain=required(distribution, epsilon, target),
               score_at_shown=score(distribution, epsilon, shown),
               score_at_plateau=score(distribution, epsilon, plateau))
    if phases:
        row["sampling_256x144"] = phase_statistics(raw, mask, epsilon, target)
    return row


def pair_contrast(raw, pairs, epsilon, shown, plateau):
    a, b = [], []
    for left, right, selected in pairs:
        a.append(raw[left][selected])
        b.append(raw[right][selected])
    a, b = np.concatenate(a), np.concatenate(b)
    if not len(a):
        return dict(pairs=0, identical_edit_rgb_fraction=None, curve=[])
    pa, pb = np.maximum(a, 0), np.maximum(b, 0)
    na, nb = np.maximum(np.minimum(a, 0), -.5), np.maximum(np.minimum(b, 0), -.5)
    slope, offset = np.abs(pa - pb), np.abs(na - nb)
    rows = []
    for gain in sorted({shown, plateau, 4.5014, 8.}):
        difference = offset + gain * slope
        # Independent direct per-endpoint transport checks both signs. No luma mixing.
        direct = np.abs(na + gain * pa - nb - gain * pb)
        assert np.allclose(difference, direct, rtol=1e-12, atol=1e-12)
        counts = np.count_nonzero(slope > 0, axis=0)
        controlled = np.count_nonzero((difference > epsilon) & (slope > 0), axis=0)
        fractions = np.divide(controlled, counts, out=np.zeros(3), where=counts > 0)
        rows.append(dict(gain=gain, controllable_pair_contrast_rgb=fractions.tolist(),
                         controllable_pair_contrast_rg_mean=float(fractions[:2].mean())))
    return dict(pairs=len(a), identical_edit_rgb_fraction=float(np.all(a == b, axis=-1).mean()),
                warning="Pair contrast is a separate exploratory feature; no no-ghosting target is fitted to it.",
                curve=rows)


def preview(motion, selected, output):
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    speed = np.sqrt(np.sum(motion * motion, axis=-1))
    grey = np.uint8(np.clip(speed / 30, 0, 1) * 255)
    left = Image.fromarray(grey).convert("RGB").resize((1000, 563), Image.Resampling.BILINEAR)
    base = np.repeat((grey * .25).astype(np.uint8)[..., None], 3, axis=2)
    widened = np.array(Image.fromarray(selected.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(3))) > 0
    base[widened] = [255, 212, 60]
    right = Image.fromarray(base).resize((1000, 563), Image.Resampling.BILINEAR)
    canvas = Image.new("RGB", (2040, 660), (19, 23, 29))
    canvas.paste(left, (10, 50))
    canvas.paste(right, (1030, 50))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 23)
    except OSError:
        font = ImageFont.load_default(size=23)
    draw.text((10, 12), "MV length: 0 px (black) to 30 px (white)", font=font, fill="white")
    draw.text((1030, 12), "Neighbouring slow / fast MV boundaries", font=font, fill="white")
    draw.text((10, 623), f"Private DLSS input {motion.shape[1]} x {motion.shape[0]}; CPU diagnostic", font=font, fill=(196, 203, 212))
    draw.text((1030, 623), "Both sides in yellow; lines widened only for display", font=font, fill=(196, 203, 212))
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--observed-plateau-gain", type=float, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview", type=Path, help="Optional local diagnostic PNG; requires Pillow")
    args = parser.parse_args()
    example_checks()
    m = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    if m["full_nr_answer"] or m["edit_saturation"] != 1 or m["depth_colour_strength"] != 0:
        parser.error("Plain edit mode required")
    if m.get("depth_weighted_positive_gain") or m.get("depth_additive_brightness") or m.get("original_guides_active"):
        raise ValueError("This analysis requires uniform gain, additive depth brightness off and matched guides")
    gain, plateau = float(m["positive_gain"]), args.observed_plateau_gain
    if not np.isfinite([gain, plateau]).all() or min(gain, plateau) <= 0:
        parser.error("Finite positive gains required")
    path = args.metadata.with_suffix(".dds")
    source = load_edit(path)
    raw = np.where(source > 0, source.astype(np.float64) / gain, source)
    motion_path = path.with_suffix(".motion.dds")
    motion = plane(motion_path, 2, 16)
    if raw.shape[:2] != motion.shape[:2] or raw.shape[:2] != (m["height"], m["width"]):
        raise ValueError("Mismatched grids")
    if not np.isfinite(motion).all() or not (np.abs(motion) < 1e6).all():
        raise ValueError("Invalid motion; do not silently classify it")
    if .5 + np.maximum(raw, 0).max() * 1024 > 65504:
        raise ValueError("Upper carrier clipping requires a different contrast reference")
    epsilon = float(np.float32(m["gain_preview_epsilon"]))
    target = .9723433127521812 if m["private_preset"] in (10,11) else .9205390681940847
    shown = float(m["preview_capture_suggested_gain"])
    if not np.isfinite([epsilon, target, shown]).all() or min(epsilon, shown) <= 0:
        raise ValueError("Finite positive epsilon and shown gain required")
    if abs(target - m["gain_preview_target_at_capture"]) > 1e-7:
        raise ValueError("Different calibration target: audit that runtime version")
    report = dict(warning="One static MV field; spatial discontinuities are not confirmed disocclusion or ghosting labels.",
        sha256={path.name: digest(path), motion_path.name: digest(motion_path), args.metadata.name: digest(args.metadata)},
        private_preset=m["private_preset"], raw_positive_gain=gain, user_plateau_this_scene=plateau,
        ghost_free_gain=None, shown=shown, epsilon=epsilon, unchanged_target=target,
        rule="Squared vector difference > delta^2; min speed <= max speed / 2; max speed >= 2 px. H/V neighbours only.",
        cases=[])
    for radius in (1, 4, 8):
        for delta in (2., 4.):
            slow, fast, degree, pairs = boundaries(motion, radius, delta)
            row = dict(radius=radius, delta=delta, selected_pairs=sum(int(v.sum()) for _, _, v in pairs))
            row["both"] = region(raw, slow | fast, epsilon, target, shown, plateau, phases=True)
            row["slow"] = region(raw, slow, epsilon, target, shown, plateau)
            row["fast"] = region(raw, fast, epsilon, target, shown, plateau)
            if delta == 2:
                row["pair_contrast"] = pair_contrast(raw, pairs, epsilon, shown, plateau)
            if radius == 1 and delta == 2:
                row["interleaving"] = [dict(minimum_different_neighbours=n,
                    **region(raw, degree >= n, epsilon, target, shown, plateau, phases=True)) for n in (2, 3, 4)]
                if args.preview:
                    preview(motion, slow | fast, args.preview)
            report["cases"].append(row)
            proposed = row["both"]["raw_suggested_gain"]
            label = f"{proposed:.6f}x" if proposed is not None else "unavailable"
            print(f"radius={radius} delta={delta:g}: {row['both']['pixel_fraction'] * 100:.4f}% pixels; suggested {label}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
