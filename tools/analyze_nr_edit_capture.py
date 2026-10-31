"""CPU audit of a pre-DLSS edit bundle against its recorded multiplier preview.

Exports derived statistics and hashes only. It does not label a frame ghost-free,
refit the runtime controller or execute DLSS/GPU work. Requires NumPy.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import load_edit


def plane(path, channels, dxgi):
    with path.open("rb") as stream:
        header = stream.read(148)
    if len(header) != 148 or header[:4] != b"DDS " or header[84:88] != b"DX10":
        raise ValueError(f"Invalid DDS: {path.name}")
    height, width, pitch = struct.unpack_from("<III", header, 12)
    if (struct.unpack_from("<I", header, 4)[0] != 124
            or struct.unpack_from("<I", header, 76)[0] != 32
            or struct.unpack_from("<5I", header, 128) != (dxgi, 3, 0, 1, 0)
            or not width or not height or pitch != width * channels * 4
            or path.stat().st_size != 148 + width * height * channels * 4):
        raise ValueError(f"Expected one tightly packed float plane: {path.name}")
    return np.memmap(path, mode="r", dtype="<f4", offset=148, shape=(height, width, channels))


def positive_rg(edit):
    return tuple(np.sort(edit[..., c][edit[..., c] > 0]) for c in range(2))


def score(positive, epsilon, gain):
    if any(not len(values) for values in positive):
        return None
    return sum(1 - np.searchsorted(values, epsilon / gain, side="right") / len(values)
               for values in positive) / 2


def required(positive, epsilon, target):
    if score(positive, epsilon, 1024) is None or score(positive, epsilon, 1024) <= target:
        return None
    lo, hi = 0., 1024.
    for _ in range(45):
        mid = (lo + hi) / 2
        if score(positive, epsilon, mid) > target:
            hi = mid
        else:
            lo = mid
    return hi


def histogram_estimate(positive, epsilon, target):
    base = 0x33800000 >> 18
    distributions = []
    for values in positive:
        if not len(values):
            return None
        bits = np.asarray(values, dtype=np.float32).view(np.uint32).astype(np.int64) >> 18
        bins = np.where(bits < base, 0, 1 + np.minimum(1022, bits - base))
        counts = np.bincount(bins, minlength=1024).astype(np.float64)
        distributions.append(counts / counts.sum())
    index = int(np.flatnonzero(np.cumsum((distributions[0] + distributions[1]) * .5) > 1 - target)[0])
    if index in (0, 1023):
        return None
    lower = np.array([(base + index - 1) << 18], dtype=np.uint32).view(np.float32)[0]
    return float(np.nextafter(np.float32(epsilon / lower), np.float32(np.inf)))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--observed-plateau-gain", type=float, required=True,
                        help="User-observed diminishing returns, NOT a ghost-free label")
    parser.add_argument("--residual-ghosting", action="store_true",
                        help="Explicit user confirmation that ghosting remains at the plateau")
    parser.add_argument("--clear-reference", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    m = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    if (m.get("edit_stage") != "after_positive_gain_before_colour_and_clipping"
            or m.get("full_nr_answer") or m.get("edit_saturation") != 1 or m.get("depth_colour_strength") != 0):
        parser.error("This audit supports plain edit mode only")
    if m.get("depth_weighted_positive_gain") or m.get("depth_additive_brightness") or m.get("original_guides_active"):
        raise ValueError("This analysis requires uniform gain, additive depth brightness off and matched guides")
    gain, plateau = float(m["positive_gain"]), args.observed_plateau_gain
    if not np.isfinite([gain, plateau]).all() or min(gain, plateau) <= 0:
        parser.error("Finite positive gains required")
    path = args.metadata.with_suffix(".dds")
    saved = load_edit(path)
    raw = np.where(saved > 0, saved.astype(np.float64) / gain, saved)
    height, width = raw.shape[:2]
    if (width, height) != (m["width"], m["height"]):
        raise ValueError("DDS dimensions differ from metadata")
    epsilon = float(np.float32(m["gain_preview_epsilon"]))
    target = .9723433127521812 if m["private_preset"] == 11 else .9205390681940847
    if abs(target - m["gain_preview_target_at_capture"]) > 1e-7:
        raise ValueError("Different calibration target: audit the corresponding runtime version")
    positive = positive_rg(raw)
    channels = [np.sort(raw[..., c][raw[..., c] > 0]) for c in range(3)]
    phases = []
    for py in range(8):
        for px in range(8):
            x = ((np.arange(256) * 16 + px * 2 + 1) * width) // (256 * 16)
            y = ((np.arange(144) * 16 + py * 2 + 1) * height) // (144 * 16)
            phases.append(histogram_estimate(positive_rg(raw[y[:, None], x]), epsilon, target))
    gains = sorted(set([1., 2., 2.25, float(m["preview_capture_suggested_gain"]), 2.3125, 2.32,
                        3., 4., 5., plateau, 8., 10.]))
    curve = []
    counts = (raw > 0).sum(axis=(0, 1))
    for mult in gains:
        transported = np.minimum(raw, 0) + mult * np.maximum(raw, 0)
        carrier = np.clip(.5 + transported, 0, 65504).astype(np.float16).astype(np.float64)
        raw_f = (transported > epsilon).sum(axis=(0, 1)) / counts
        half_f = (carrier - .5 > epsilon).sum(axis=(0, 1)) / counts
        curve.append(dict(gain=mult, conditional_positive_rgb_raw=raw_f.tolist(),
                          conditional_positive_rgb_fp16=half_f.tolist(), score_rg_raw=float(raw_f[:2].mean()),
                          score_rg_fp16=float(half_f[:2].mean()),
                          neutral_rgb_fp16=np.mean(np.abs(carrier - .5) <= epsilon, axis=(0, 1)).tolist()))
    base = np.clip(.5 + np.minimum(raw, 0), 0, 65504).astype(np.float16).astype(np.float64)
    unboostable = ((raw <= 0) & (np.abs(base - .5) <= epsilon)).mean(axis=(0, 1))
    report = dict(
        warning="Single static capture; a user-observed improvement plateau is not a no-ghosting threshold. No NGX execution.",
        observation=dict(plateau_gain=plateau, residual_ghosting=True if args.residual_ghosting else None, ghost_free_gain=None),
        metadata=m, sha256={path.name: digest(path), args.metadata.name: digest(args.metadata)},
        independent_channel_suggested_gains=[required((v, v), epsilon, target) for v in channels],
        exact_full_raw_suggested_gain=required(positive, epsilon, target),
        full_histogram_suggested_gain=histogram_estimate(positive, epsilon, target),
        phase_256x144_suggested_gains=phases,
        source_positive_fraction_rgb=(raw > 0).mean(axis=(0, 1)).tolist(),
        source_zero_fraction_rgb=(raw == 0).mean(axis=(0, 1)).tolist(),
        unboostable_neutral_fraction_rgb=unboostable.tolist(), curve=curve)
    report["positive_quantiles_rgb"] = [np.quantile(raw[..., c][raw[..., c] > 0],
        [0, .01, 1 - target, .05, .5, .9, .99, 1]).tolist() for c in range(3)]
    report["quantile_levels"] = [0, .01, 1 - target, .05, .5, .9, .99, 1]
    depth_path, motion_path = path.with_suffix(".depth.dds"), path.with_suffix(".motion.dds")
    depth, motion = plane(depth_path, 1, 41)[..., 0], plane(motion_path, 2, 16)
    if depth.shape != raw.shape[:2] or motion.shape[:2] != raw.shape[:2]:
        raise ValueError("Guide grid differs from the edit grid")
    if not np.isfinite(depth).all() or not np.isfinite(motion).all():
        raise ValueError("Invalid guide data; do not silently segment it")
    speed = np.hypot(motion[..., 0], motion[..., 1])
    report["motion_speed_quantiles"] = np.quantile(speed, [0, .01, .1, .5, .9, .99, 1]).tolist()
    report["regions"] = []
    for name, mask in (("depth_zero", depth == 0), ("depth_nonzero", depth > 0),
                       ("speed_below_1", speed < 1), ("speed_1_to_10", (speed >= 1) & (speed < 10)),
                       ("speed_10_to_20", (speed >= 10) & (speed < 20)), ("speed_above_20", speed >= 20)):
        region = positive_rg(raw[mask])
        report["regions"].append(dict(name=name, pixel_fraction=float(mask.mean()),
            positive_counts_rg=[len(v) for v in region],
            suggested_gain=required(region, epsilon, target),
            score_at_shown=score(region, epsilon, float(m["preview_capture_suggested_gain"])),
            score_at_plateau=score(region, epsilon, plateau)))
    report["sha256"].update({depth_path.name: digest(depth_path), motion_path.name: digest(motion_path)})
    # A counterfactual diagnostic only. Do not refit the binary clear/bad dataset
    # using this new frame, which has NOT been labelled ghost-free.
    new_target = score(positive, epsilon, plateau)
    references = []
    for reference in args.clear_reference:
        family = "transformer" if m["private_preset"] == 11 else "cnn"
        if "noghosting" not in reference.stem or f"-{family}-" not in reference.stem:
            raise ValueError("Reference must be a same-family legacy capture explicitly labelled noghosting")
        distribution = positive_rg(load_edit(reference))
        references.append(dict(file=reference.name, sha256=digest(reference),
            current_target_relative_gain=required(distribution, epsilon, target),
            plateau_target_relative_gain=required(distribution, epsilon, new_target)))
    report["counterfactual_target"] = dict(target=new_target, known_clear_references=references,
        warning="These are relative to the already multiplied old clear captures; not absolute slider values.")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Full raw {report['exact_full_raw_suggested_gain']:.9f}x; histogram {report['full_histogram_suggested_gain']:.9f}x")
    print(f"64 phases {min(phases):.9f}..{max(phases):.9f}x; user plateau {plateau:g}x, residual ghosting: {args.residual_ghosting}")


if __name__ == "__main__":
    main()
