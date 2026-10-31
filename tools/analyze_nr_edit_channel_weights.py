"""Nonnegative RGB weights on per-channel positive-edit coverage; exploratory fit.

Only one common RGB weight vector for CNN/transformer, with one target per family.
The fixed epsilon comes from the preceding study; no production rendering edits.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import edit_paths, load_edit
from analyze_nr_edit_channel_coverage import prepare, coverage, solve


def weight_grid(denominator):
    for r in range(denominator + 1):
        for g in range(denominator - r + 1):
            yield (r / denominator, g / denominator, (denominator - r - g) / denominator)


def fit(rows, fractions, weights, epsilon):
    # Use the identical scalar dot order as the root evaluator. A one-ULP
    # matrix/vector difference can otherwise falsely cross a flat coverage step.
    scores = np.array([float(np.dot(fraction, weights)) for fraction in fractions])
    families = {}
    for model in ("cnn", "transformer"):
        indices = [i for i, row in enumerate(rows) if row["model"] == model]
        good = [i for i in indices if not rows[i]["ghosting"]]
        bad = [i for i in indices if rows[i]["ghosting"]]
        target = float(np.nextafter(max(scores[i] for i in indices), np.inf))
        gains = [solve(rows[i], epsilon, target, "positive_active", weights) for i in good]
        if any(g is None for g in gains):
            return None
        if any(g < 1 - 1e-8 for g in gains):
            raise AssertionError("Full-frame target must not lower an observed first-clear gain")
        families[model] = dict(target=float(target), gains=gains, worst=max(gains),
            clear_files=[rows[i]["file"] for i in good], clear_scores=[float(scores[i]) for i in good],
            bad_scores=[float(scores[i]) for i in bad],
            labels_separable=bool(max(scores[i] for i in bad) < min(scores[i] for i in good)))
    return dict(mode="positive_active", aggregation="weighted", weights=list(weights), epsilon=epsilon,
                families=families, worst=max(f["worst"] for f in families.values()))


def root_average_fit(breakdown, weights):
    families = {}
    for model in ("cnn", "transformer"):
        group = [r for r in breakdown["files"] if r["model"] == model]
        values = [(r, float(np.dot(r["gain_by_each_channel_at_parent_target"], weights))) for r in group]
        good = [(r, v) for r, v in values if not r["ghosting"]]
        bad = [(r, v) for r, v in values if r["ghosting"]]
        scale = max(1 / min(v for r, v in good), np.nextafter(1 / min(v for r, v in bad), np.inf))
        while min(v * scale for r, v in good) < 1 or min(v * scale for r, v in bad) <= 1:
            scale = np.nextafter(scale, np.inf)
        gains = [float(v * scale) for r, v in good]
        families[model] = dict(scale=float(scale), gains=gains, worst=max(gains), clear_files=[r["file"] for r, v in good])
    return dict(weights=list(weights), families=families, worst=max(f["worst"] for f in families.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epsilon", type=float, default=.008791791791791792)
    parser.add_argument("--denominator", type=int, default=40, help="Simplex grid: default weights every .025")
    parser.add_argument("--breakdown", type=Path)
    args = parser.parse_args()
    if not np.isfinite(args.epsilon) or args.epsilon <= 0 or not 1 <= args.denominator <= 200:
        parser.error("Positive finite epsilon and denominator 1..200 required")
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edits matched")
    rows = [prepare(path, load_edit(path)) for path in paths]
    fractions = np.array([coverage(row, args.epsilon, 1., "positive_active") for row in rows])
    weights = list(weight_grid(args.denominator))
    weights += [(1 / 3, 1 / 3, 1 / 3), (.2126, .7152, .0722), (.25, .5, .25)]
    candidates = []
    for index, weight in enumerate(weights):
        result = fit(rows, fractions, weight, args.epsilon)
        if result:
            candidates.append(result)
        if index % 100 == 0:
            print(f"weights {index}/{len(weights)}", flush=True)
    candidates.sort(key=lambda row: (row["worst"], sum(f["worst"] for f in row["families"].values()), row["weights"][2]))
    best = candidates[0]
    # Local refinement only, not an exhaustive fine-grid/global optimum claim.
    center = np.rint(np.array(best["weights"]) * 200).astype(int)
    refinement = []
    for r in range(max(0, center[0] - 10), min(200, center[0] + 10) + 1):
        for g in range(max(0, center[1] - 10), min(200 - r, center[1] + 10) + 1):
            refinement.append((r / 200, g / 200, (200 - r - g) / 200))
    for weight in refinement:
        result = fit(rows, fractions, weight, args.epsilon)
        if result:
            candidates.append(result)
    candidates.sort(key=lambda row: (row["worst"], sum(f["worst"] for f in row["families"].values()), row["weights"][2]))
    report = dict(warning="Post-hoc weight selection on the same 16 static captures, no temporal/FP16 or independent validation.",
        sampling="full raw captured edit", epsilon=args.epsilon, denominator=args.denominator,
        coarse_weight_count=len(weights), local_refinement_count=len(refinement),
        candidates=candidates)
    if args.breakdown:
        breakdown = json.loads(args.breakdown.read_text(encoding="utf-8"))
        root_results = [root_average_fit(breakdown, w) for w in weights]
        root_results.sort(key=lambda row: row["worst"])
        root_center = np.rint(np.array(root_results[0]["weights"]) * 200).astype(int)
        for r in range(max(0, root_center[0] - 10), min(200, root_center[0] + 10) + 1):
            for g in range(max(0, root_center[1] - 10), min(200 - r, root_center[1] + 10) + 1):
                root_results.append(root_average_fit(breakdown, (r / 200, g / 200, (200 - r - g) / 200)))
        root_results.sort(key=lambda row: (row["worst"], sum(f["worst"] for f in row["families"].values()), row["weights"][2]))
        report["weighted_gain_roots"] = dict(warning="Different rule: weighted mean of prior channel gain proposals, then one-sided scale per family.",
                                            best=root_results[0])
    # Threshold transfer audit only: weights were selected on all images, so
    # this is NOT an independent holdout of the weight-selection procedure.
    chosen = candidates[0]["weights"]
    current_scores = [float(np.dot(f, chosen)) for f in fractions]
    transfer = []
    for training_game in ("game1", "game2"):
        for model in ("cnn", "transformer"):
            training = [i for i, row in enumerate(rows) if row["model"] == model and row["file"].startswith(training_game + "-")]
            target = float(np.nextafter(max(current_scores[i] for i in training), np.inf))
            test = [i for i, row in enumerate(rows) if row["model"] == model and not row["file"].startswith(training_game + "-")]
            accepted_bad = [rows[i]["file"] for i in test if rows[i]["ghosting"] and current_scores[i] > target]
            gains = {rows[i]["file"]: solve(rows[i], args.epsilon, target, "positive_active", chosen)
                     for i in test if not rows[i]["ghosting"]}
            transfer.append(dict(training_game=training_game, model=model, target=target,
                                 accepted_known_bad=accepted_bad, first_clear_gain_ratios=gains))
    report["threshold_transfer"] = dict(warning="Weights selected on all frames; not an independent holdout.", files=transfer)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("BEST COVERAGE", candidates[0])
    if args.breakdown:
        print("BEST GAIN ROOTS", root_results[0])


if __name__ == "__main__":
    main()
