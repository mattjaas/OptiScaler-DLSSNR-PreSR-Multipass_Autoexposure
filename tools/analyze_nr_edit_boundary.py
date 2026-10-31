"""Audit gain prediction when supplied 'noghosting' captures are first-clear boundaries.

A separating classifier threshold is not necessarily a minimum-gain target.
Requires NumPy and a report from analyze_nr_edit_common.py. Optional --dds
also reproduces a rejected, spatially normalized common-score experiment.
"""

import argparse
import glob
import hashlib
import json
import math
from pathlib import Path

import numpy as np

from analyze_nr_edit_common import measure
from analyze_nr_edit_ghosting import load_edit


def boundary_range(values, classification_target=None):
    if not values or any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("Boundary scores must be finite and positive")
    low, high = min(values), max(values)
    target = math.sqrt(low * high)
    result = dict(min_score=low, max_score=high, max_min_ratio=high / low,
                  diagnostic_minimax_target=target,
                  diagnostic_minimax_gain_ratio_min=target / high,
                  diagnostic_minimax_gain_ratio_max=target / low,
                  target_reaching_all_observed_boundaries=high,
                  maximum_overgain_factor_with_that_target=high / low)
    if classification_target is not None:
        result["previous_classification_target"] = classification_target
        result["predicted_gain_over_observed_boundary_gain"] = [classification_target / v for v in values]
    return result


def spatial_score(rgb, neighbor_x=None, neighbor_y=None):
    positive = np.maximum(np.asarray(rgb, dtype=np.float64), 0)
    terms = measure(positive)
    signal_energy = float(np.mean(positive * positive))
    if neighbor_x is None:
        dx = np.diff(positive, axis=1)
        dy = np.diff(positive, axis=0)
        # Clamped +1 point samples: the final row/column contributes zero,
        # and remains in the full denominator, matching sparse sampling.
        gradient_energy = float((np.sum(dx * dx) + np.sum(dy * dy)) / (positive.size * 2))
    else:
        dx = np.maximum(np.asarray(neighbor_x, dtype=np.float64), 0) - positive
        dy = np.maximum(np.asarray(neighbor_y, dtype=np.float64), 0) - positive
        gradient_energy = float((np.mean(dx * dx) + np.mean(dy * dy)) / 2)
    if not signal_energy or not gradient_energy:
        raise ValueError("Spatial coherence is undefined for this constant/empty signal")
    coherence = math.sqrt(signal_energy / gradient_energy)
    return float(coherence * (3.25 * terms["mean_lower_chroma"] + .5 * terms["occupancy_correction"]
                             + terms["pixel_min_mean"]) + terms["amplitude"] / 128)


def analyse(study, paths):
    report = dict(
        interpretation="The user states noghosting captures are approximately the first disappearance, not safe headroom.",
        pair_design="One pair changes only positive gain; the other changes only model input brightness. Number mapping not specified.",
        warning="The previous RGB classifier is rejected as a minimum-gain controller. No new validated controller is claimed.",
        shared_rgb_score={}, rejected_spatial_candidate={})
    for model, classification_target in study["thresholds"].items():
        clear = [r for r in study["files"] if r["model"] == model and not r["ghosting"]]
        values = [r["full"]["score"] for r in clear]
        report["shared_rgb_score"][model] = dict(
            files=[r["file"] for r in clear], scores=values,
            **boundary_range(values, classification_target))
    if paths:
        known = {r["file"]: r for r in study["files"]}
        spatial = []
        for path in paths:
            source = known[path.name]
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if digest != source["sha256"]:
                raise ValueError(f"Source changed: {path.name}")
            rgb = load_edit(path)
            h, w = rgb.shape[:2]
            row = dict(file=path.name, model=source["model"], ghosting=source["ghosting"],
                       score=spatial_score(rgb), sampling={})
            for nx, ny in ((64, 36), (128, 72)):
                values = []
                for py in range(8):
                    for px in range(8):
                        x = ((np.arange(nx) * 16 + px * 2 + 1) * w) // (nx * 16)
                        y = ((np.arange(ny) * 16 + py * 2 + 1) * h) // (ny * 16)
                        values.append(spatial_score(rgb[y[:, None], x],
                                                    rgb[y[:, None], np.minimum(x + 1, w - 1)],
                                                    rgb[np.minimum(y[:, None] + 1, h - 1), x]))
                row["sampling"][f"{nx}x{ny}"] = dict(min=min(values), max=max(values))
            spatial.append(row)
            print(f"{path.name}: spatial experiment complete", flush=True)
        candidate = report["rejected_spatial_candidate"]
        candidate["warning"] = "Post-hoc fit to full boundary frames; rejected after sparse sampling, not implemented on GPU."
        candidate["files"] = spatial
        candidate["families"] = {}
        for model in study["thresholds"]:
            rows = [r for r in spatial if r["model"] == model]
            clear = [r for r in rows if not r["ghosting"]]
            summary = boundary_range([r["score"] for r in clear])
            target = summary["diagnostic_minimax_target"]
            summary["full_frame_classes_separable"] = max(r["score"] for r in rows if r["ghosting"]) < min(r["score"] for r in clear)
            summary["sparse_gain_ratio_ranges"] = {}
            for grid in ("64x36", "128x72"):
                summary["sparse_gain_ratio_ranges"][grid] = dict(
                    min=min(target / r["sampling"][grid]["max"] for r in clear),
                    max=max(target / r["sampling"][grid]["min"] for r in clear))
            candidate["families"][model] = summary
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--first-clear-is-boundary", action="store_true", required=True,
                        help="Explicitly acknowledge this interpretation of supplied labels")
    parser.add_argument("--dds", nargs="*", default=[], help="Optional paths/globs for spatial rejection experiment")
    args = parser.parse_args()
    study = json.loads(args.study.read_text(encoding="utf-8"))
    paths = sorted({Path(p) for pattern in args.dds for p in glob.glob(pattern)})
    if args.dds and not paths:
        parser.error("No DDS files matched")
    report = analyse(study, paths)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for model, r in report["shared_rgb_score"].items():
        print(f"{model}: boundary max/min={r['max_min_ratio']:.6f}, "
              f"best constant target gain ratio={r['diagnostic_minimax_gain_ratio_min']:.6f}.."
              f"{r['diagnostic_minimax_gain_ratio_max']:.6f}")


if __name__ == "__main__":
    main()
