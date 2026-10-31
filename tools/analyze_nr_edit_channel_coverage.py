"""Per-channel coverage around private DLSS neutral 0.5; exploratory CPU study.

Positive gain is applied per RGB component, with negative components unchanged.
No luma conversion, FP16 rounding, upper cap or temporal model. Requires NumPy.
Labels come from filenames. No raw pixels or absolute paths exported.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import edit_paths, load_edit

MODES = ("positive", "both", "positive_active", "both_active")
AGGREGATIONS = ("min", "median", "mean", "geomean", "rms", "max")


def prepare(path, edit):
    edit = np.asarray(edit, dtype=np.float64).reshape(-1, 3)
    positive, negative = np.maximum(edit, 0), np.minimum(-np.minimum(edit, 0), .5)
    return dict(file=path.name, model=path.stem.split("-")[1], ghosting="noghosting" not in path.stem,
        count=len(edit), positive=[np.sort(positive[:, c]) for c in range(3)],
        negative=[np.sort(negative[:, c]) for c in range(3)],
        positive_count=np.count_nonzero(positive, axis=0), active_count=np.count_nonzero(positive + negative, axis=0))


def aggregate(values, name):
    if isinstance(name, (tuple, list, np.ndarray)):
        return float(np.dot(values, name))
    if name in ("top2_mean", "bottom2_mean"):
        ordered = np.sort(values)
        return float(np.mean(ordered[1:] if name == "top2_mean" else ordered[:2]))
    if name == "rg_mean":
        return float((values[0] + values[1]) * .5)
    if name in ("r", "g", "b"):
        return float(values[("r", "g", "b").index(name)])
    if name == "min":
        return float(np.min(values))
    if name == "max":
        return float(np.max(values))
    if name == "median":
        return float(np.median(values))
    if name == "mean":
        return float(np.mean(values))
    if name == "geomean":
        return float(np.prod(values) ** (1 / 3))
    return float(np.sqrt(np.mean(values * values)))


def coverage(row, epsilon, relative_gain, mode):
    positive = np.array([row["count"] - np.searchsorted(c, epsilon / relative_gain, side="right")
                         for c in row["positive"]], dtype=np.float64)
    if mode == "positive":
        return positive / row["count"]
    if mode == "positive_active":
        return np.divide(positive, row["positive_count"], out=np.zeros(3), where=row["positive_count"] > 0)
    negative = np.array([row["count"] - np.searchsorted(c, epsilon, side="right")
                         for c in row["negative"]], dtype=np.float64)
    if mode == "both":
        return (positive + negative) / row["count"]
    return np.divide(positive + negative, row["active_count"], out=np.zeros(3), where=row["active_count"] > 0)


def solve(row, epsilon, target, mode, aggregation):
    low, high = 0., 1024.
    if aggregate(coverage(row, epsilon, high, mode), aggregation) <= target:
        return None
    for _ in range(42):
        mid = (low + high) / 2
        if aggregate(coverage(row, epsilon, mid, mode), aggregation) > target:
            high = mid
        else:
            low = mid
    return high


def screen(rows, modes, aggregations, epsilons):
    results = []
    for mode in modes:
        for aggregation in aggregations:
            for epsilon in epsilons:
                fractions = [coverage(row, epsilon, 1., mode) for row in rows]
                scores = [aggregate(v, aggregation) for v in fractions]
                families = {}
                for model in ("cnn", "transformer"):
                    indices = [i for i, row in enumerate(rows) if row["model"] == model]
                    good = [i for i in indices if not rows[i]["ghosting"]]
                    bad = [i for i in indices if rows[i]["ghosting"]]
                    if not good or not bad:
                        raise ValueError(f"Need both labels for {model}")
                    target = max(scores[i] for i in indices)
                    gains = [solve(rows[i], epsilon, target, mode, aggregation) for i in good]
                    if any(g is None for g in gains):
                        break
                    families[model] = dict(target=target, gains=gains, worst=max(gains),
                        clear_coverage=[fractions[i].tolist() for i in good],
                        clear_files=[rows[i]["file"] for i in good], bad_scores=[scores[i] for i in bad],
                        clear_scores=[scores[i] for i in good], labels_separable=max(scores[i] for i in bad) < min(scores[i] for i in good))
                if len(families) == 2:
                    results.append(dict(mode=mode, aggregation=aggregation, epsilon=float(epsilon), families=families,
                                        worst=max(f["worst"] for f in families.values())))
            print(f"{mode}/{aggregation}: complete", flush=True)
    return sorted(results, key=lambda row: row["worst"])


def phases(paths, candidate, grid=(64, 36)):
    nx, ny = grid
    rows = []
    for path in paths:
        if "noghosting" not in path.name:
            continue
        edit = load_edit(path)
        height, width = edit.shape[:2]
        target = candidate["families"][path.stem.split("-")[1]]["target"]
        gains = []
        for py in range(8):
            for px in range(8):
                x = ((np.arange(nx) * 16 + px * 2 + 1) * width) // (nx * 16)
                y = ((np.arange(ny) * 16 + py * 2 + 1) * height) // (ny * 16)
                row = prepare(path, edit[y[:, None], x])
                gains.append(solve(row, candidate["epsilon"], target, candidate["mode"], candidate.get("weights", candidate["aggregation"])))
        finite = [g for g in gains if g is not None]
        rows.append(dict(file=path.name, minimum=min(finite) if finite else None,
                         maximum=max(finite) if finite else None, phases=gains))
    return dict(warning="Targets fitted to same full frames; no temporal validation.",
                weights=candidate.get("weights"),
                epsilon=candidate["epsilon"], mode=candidate["mode"], aggregation=candidate["aggregation"],
                grid=list(grid), files=rows)


def channel_breakdown(paths, candidate):
    if candidate["aggregation"] != "median" or candidate["mode"] != "positive_active":
        raise ValueError("Breakdown expects the prior positive_active/median candidate")
    rows = [prepare(path, load_edit(path)) for path in paths]
    epsilon, mode = candidate["epsilon"], candidate["mode"]
    fractions = [coverage(row, epsilon, 1., mode) for row in rows]
    independent_targets = {}
    for model in ("cnn", "transformer"):
        group = [fraction for row, fraction in zip(rows, fractions) if row["model"] == model]
        independent_targets[model] = np.max(group, axis=0).tolist()
    output = []
    for row, fraction in zip(rows, fractions):
        model = row["model"]
        target = candidate["families"][model]["target"]
        parent = [solve(row, epsilon, target, mode, channel) for channel in ("r", "g", "b")]
        independent = [solve(row, epsilon, independent_targets[model][c], mode, channel)
                       for c, channel in enumerate(("r", "g", "b"))]
        median_gain = float(np.median(parent)) if all(g is not None for g in parent) else None
        selected = "/".join(channel.upper() for channel, gain in zip(("r", "g", "b"), parent)
                            if gain is not None and median_gain is not None and abs(gain - median_gain) <= 1e-8)
        output.append(dict(file=row["file"], model=model, ghosting=row["ghosting"],
            coverage_rgb=fraction.tolist(), parent_common_target=target,
            gain_by_each_channel_at_parent_target=parent, parent_median_gain=median_gain,
            median_channel=selected, independent_channel_targets=independent_targets[model],
            independent_channel_gains=independent))
    for model in ("cnn", "transformer"):
        for reference in candidate["families"][model]["clear_files"]:
            row = next(r for r in output if r["file"] == reference)
            index = candidate["families"][model]["clear_files"].index(reference)
            if abs(row["parent_median_gain"] - candidate["families"][model]["gains"][index]) > 1e-8:
                raise AssertionError("Channel root decomposition does not reproduce prior median result")
    return dict(warning="Breakdown of prior same-data fitted median metric, no runtime/temporal validation. Per-channel roots are hypothetical scoring choices, not separate RGB controls.",
                epsilon=epsilon, mode=mode, sampling="full raw FP32 captured edit; no FP16 quantization in this prior metric",
                meaning="Coverage is count(E_channel > epsilon)/count(E_channel > 0). A ratio scales only positive captured edit.",
                independent_targets="For each model/channel: max coverage across all supplied clear/bad frames; accept strictly above.",
                files=output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--full", action="store_true")
    reference_group = parser.add_mutually_exclusive_group()
    reference_group.add_argument("--phase-reference", type=Path, help="Test 64 phases of first candidate from full report")
    reference_group.add_argument("--channel-breakdown-reference", type=Path, help="Decompose the prior full median candidate into R/G/B roots")
    parser.add_argument("--grid", nargs=2, type=int, default=[512, 288])
    parser.add_argument("--modes", nargs="+", choices=MODES, default=list(MODES))
    parser.add_argument("--aggregations", nargs="+", choices=AGGREGATIONS + ("r", "g", "b", "rg_mean", "top2_mean", "bottom2_mean"), default=list(AGGREGATIONS))
    parser.add_argument("--epsilon", nargs="+", type=float)
    parser.add_argument("--phase-grid", nargs=2, type=int, default=[64, 36])
    args = parser.parse_args()
    epsilons = np.asarray(args.epsilon if args.epsilon is not None else np.geomspace(.00025, 2, 100))
    if not np.isfinite(epsilons).all() or np.any(epsilons <= 0) or min(args.grid) < 1 or min(args.phase_grid) < 1:
        parser.error("Finite positive epsilon/grid required")
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edits matched")
    if args.channel_breakdown_reference is not None:
        candidate = json.loads(args.channel_breakdown_reference.read_text(encoding="utf-8"))["candidates"][0]
        report = channel_breakdown(paths, candidate)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return
    if args.phase_reference is not None:
        candidate = json.loads(args.phase_reference.read_text(encoding="utf-8"))["candidates"][0]
        report = phases(paths, candidate, args.phase_grid)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return
    rows = []
    for path in paths:
        edit = load_edit(path)
        height, width = edit.shape[:2]
        if not args.full:
            nx, ny = args.grid
            x = (np.arange(nx) * 2 + 1) * width // (nx * 2)
            y = (np.arange(ny) * 2 + 1) * height // (ny * 2)
            edit = edit[y[:, None], x]
        rows.append(prepare(path, edit))
        print(f"{path.name}: loaded", flush=True)
    results = screen(rows, args.modes, args.aggregations, epsilons)
    report = dict(warning="Exploratory same-data fit; no independent temporal/FP16 validation or runtime controller.",
        input_mode="plain residual, no saturation/depth colour; positive multiplier only",
        sampling="full" if args.full else args.grid, epsilons=epsilons.tolist(), candidates=results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if results:
        r = results[0]
        print(f"Best screened: {r['mode']}/{r['aggregation']}, epsilon={r['epsilon']}, worst gain ratio={r['worst']}")


if __name__ == "__main__":
    main()
