"""Exploratory, labelled DDS study; this does not validate a temporal ghosting detector.

Requires numpy. Accept explicit DDS paths or quoted glob patterns. No pixels or
absolute source paths are written to the report. Labels come from file names,
never from a detector. Run --help for the sampling and decision parameters.
"""

import argparse
import csv
import glob
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np


def edit_paths(patterns):
    """Expand raw-edit inputs, ignoring diagnostic depth/motion companions."""
    return sorted({Path(p) for pattern in patterns for p in glob.glob(pattern)
                   if not p.lower().endswith((".depth.dds", ".motion.dds"))})


def load_edit(path):
    with path.open("rb") as stream:
        header = stream.read(148)
    if len(header) != 148 or header[:4] != b"DDS ":
        raise ValueError(f"{path.name}: missing DDS header")
    height, width, pitch = struct.unpack_from("<III", header, 12)
    if (struct.unpack_from("<I", header, 4)[0] != 124
            or struct.unpack_from("<I", header, 76)[0] != 32
            or header[84:88] != b"DX10"
            or struct.unpack_from("<5I", header, 128) != (2, 3, 0, 1, 0)
            or not width or not height or pitch != width * 16
            or path.stat().st_size != 148 + width * height * 16):
        raise ValueError(f"{path.name}: expected one tight R32G32B32A32_FLOAT 2D edit")
    rgba = np.memmap(path, mode="r", dtype="<f4", offset=148, shape=(height, width, 4))
    if not np.isfinite(rgba[..., :3]).all():
        raise ValueError(f"{path.name}: nonfinite edit; do not silently label it")
    return rgba[..., :3]


def percentile(values, fraction):
    flat = values.ravel()
    rank = math.ceil(fraction * flat.size) - 1
    return float(np.partition(flat, rank)[rank])


def percentile90(values):
    return percentile(values, .9)


def sample_amplitude(amplitude, nx, ny, phase_x, phase_y):
    # Cell-stratified, point sampling, no filtering. Matches the GPU probe's
    # integer arithmetic exactly; each phase is in 0..7.
    height, width = amplitude.shape
    x = ((np.arange(nx) * 16 + phase_x * 2 + 1) * width) // (nx * 16)
    y = ((np.arange(ny) * 16 + phase_y * 2 + 1) * height) // (ny * 16)
    return amplitude[y[:, None], x]


def feature_summary(path, threshold, target, coverage_threshold=.1):
    rgb = load_edit(path)
    positive = np.maximum(rgb, 0)
    amplitude = positive.max(axis=-1)
    p90 = percentile90(amplitude)
    p50 = percentile(amplitude, .5)
    selected_quantile = percentile(amplitude, 1 - coverage_threshold)
    ghost = "noghosting" not in path.stem
    if not any(word in path.stem for word in ("ghosting", "noghosting")):
        raise ValueError(f"{path.name}: filename must supply a ghosting/noghosting label")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    gradients = [np.abs(np.diff(positive, axis=axis)).mean(dtype=np.float64)
                 for axis in (0, 1) if positive.shape[axis] > 1]
    row = dict(file=path.name, game=path.stem.split("-")[0], ghosting=ghost,
               sha256=digest,
               width=rgb.shape[1], height=rgb.shape[0],
               positive_mean=float(positive.mean(dtype=np.float64)),
               positive_rms=float(np.sqrt(np.mean(positive.astype(np.float64) ** 2))),
               positive_amplitude_p90=p90,
               positive_amplitude_p50=p50,
               positive_coverage=float(np.mean(amplitude > threshold)),
               negative_mean=float(np.minimum(rgb, 0).mean(dtype=np.float64)),
               gradient_positive=float(np.mean(gradients)) if gradients else 0.,
               fp16_neutral_fraction=float(np.mean(np.clip(.5 + rgb, 0, 65504).astype(np.float16) == .5)),
               negative_clip_fraction=float(np.mean(rgb < -.5)),
               fp16_lost_positive_fraction=float(np.mean(
                   np.clip(.5 + rgb[rgb > 0], 0, 65504).astype(np.float16) == .5)) if np.any(rgb > 0) else 0.,
               controller_quantile=1 - coverage_threshold,
               controller_amplitude=selected_quantile,
               proposed_relative_gain=target / selected_quantile if selected_quantile else None,
               fixed_rule_predicts_ghosting=bool(np.mean(amplitude > threshold) < coverage_threshold))
    row["sampling"] = {}
    for nx, ny in ((32, 18), (64, 36), (128, 72)):
        phases = []
        for py in range(8):
            for px in range(8):
                v = sample_amplitude(amplitude, nx, ny, px, py)
                phases.append(dict(x=px, y=py, p90=percentile90(v), p50=percentile(v, .5),
                                   coverage=float(np.mean(v > threshold))))
        row["sampling"][f"{nx}x{ny}"] = dict(
            samples=nx * ny,
            p90_min=min(p["p90"] for p in phases),
            p90_max=max(p["p90"] for p in phases),
            p50_min=min(p["p50"] for p in phases),
            p50_max=max(p["p50"] for p in phases),
            coverage_min=min(p["coverage"] for p in phases),
            coverage_max=max(p["coverage"] for p in phases),
            correct_phases=sum((p["coverage"] < coverage_threshold) == ghost for p in phases),
            phases=phases)
    return row


def fit_threshold(rows, key):
    # Deliberately just one scalar threshold. No classifier/feature search here.
    upper_ghost = max(r[key] for r in rows if r["ghosting"])
    lower_clear = min(r[key] for r in rows if not r["ghosting"])
    return dict(max_ghost=upper_ghost, min_clear=lower_clear,
                gap=lower_clear - upper_ghost,
                separates_labels=upper_ghost < lower_clear,
                threshold=(upper_ghost + lower_clear) / 2)


def analyse(paths, threshold=.14, target=.15, coverage_threshold=.1):
    rows = [feature_summary(p, threshold, target, coverage_threshold) for p in paths]
    features = ("positive_mean", "positive_rms", "positive_amplitude_p90",
                "positive_amplitude_p50", "positive_coverage", "gradient_positive", "fp16_neutral_fraction")
    study = dict(format_version=2, numpy=np.__version__,
                 warning="Post-hoc exploratory fit: labelled still frames are not independent temporal validation.",
                 amplitude_threshold=threshold, coverage_threshold=coverage_threshold,
                 proposed_target_quantile=1 - coverage_threshold, proposed_target_amplitude=target,
                 files=rows, separation={}, game_holdout={})
    games = sorted({r["game"] for r in rows})
    for key in features:
        study["separation"][key] = fit_threshold(rows, key)
        folds = []
        for game in games:
            train = [r for r in rows if r["game"] != game]
            test = [r for r in rows if r["game"] == game]
            fitted = fit_threshold(train, key)
            folds.append(dict(test_game=game, **fitted,
                              correct=sum((r[key] < fitted["threshold"]) == r["ghosting"] for r in test),
                              count=len(test)))
        study["game_holdout"][key] = folds
    return study


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="DDS paths or glob patterns; filenames supply labels and game prefix")
    parser.add_argument("--output", type=Path, required=True, help="JSON report (CSV written beside it)")
    parser.add_argument("--threshold", type=float, default=.14, help="Amplitude for coverage test; exploratory default .14")
    parser.add_argument("--target", type=float, default=.15, help="Proposed controller amplitude target; not a proven safe minimum")
    parser.add_argument("--coverage-threshold", type=float, default=.1,
                        help="Fraction that must exceed amplitude threshold (.1: P90, .5: P50)")
    args = parser.parse_args()
    if not math.isfinite(args.threshold) or args.threshold <= 0 or not math.isfinite(args.target) or args.target <= 0:
        parser.error("threshold and target must be finite and positive")
    if not math.isfinite(args.coverage_threshold) or not 0 < args.coverage_threshold < 1:
        parser.error("coverage-threshold must be between 0 and 1 (exclusive)")
    paths = edit_paths(args.files)
    if not paths:
        parser.error("no DDS files matched")
    groups = {p.stem.split("-")[0] for p in paths}
    if len(groups) < 2 or any(not any(p.stem.startswith(g + "-") and "noghosting" in p.stem for p in paths)
                              or not any(p.stem.startswith(g + "-") and "noghosting" not in p.stem for p in paths)
                              for g in groups):
        parser.error("need >=2 game prefixes, each with both labels")
    study = analyse(paths, args.threshold, args.target, args.coverage_threshold)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(study, indent=2) + "\n", encoding="utf-8")
    keys = [k for k in study["files"][0] if k != "sampling"]
    with args.output.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, keys)
        writer.writeheader()
        writer.writerows({k: v for k, v in row.items() if k != "sampling"} for row in study["files"])
    for row in study["files"]:
        print(f"{row['file']}: p90={row['positive_amplitude_p90']:.8f}, "
              f"p50={row['positive_amplitude_p50']:.8f}, "
              f"coverage={row['positive_coverage']:.6%}, "
              f"sparse correct phases={row['sampling']['64x36']['correct_phases']}/64")


if __name__ == "__main__":
    main()
