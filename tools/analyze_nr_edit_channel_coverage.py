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


def phases(paths, candidate):
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
                x = ((np.arange(64) * 16 + px * 2 + 1) * width) // (64 * 16)
                y = ((np.arange(36) * 16 + py * 2 + 1) * height) // (36 * 16)
                row = prepare(path, edit[y[:, None], x])
                gains.append(solve(row, candidate["epsilon"], target, candidate["mode"], candidate["aggregation"]))
        finite = [g for g in gains if g is not None]
        rows.append(dict(file=path.name, minimum=min(finite) if finite else None,
                         maximum=max(finite) if finite else None, phases=gains))
    return dict(warning="Targets fitted to same full frames; no temporal validation.",
                epsilon=candidate["epsilon"], mode=candidate["mode"], aggregation=candidate["aggregation"],
                grid=[64, 36], files=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--phase-reference", type=Path, help="Test 64 phases of first candidate from full report")
    parser.add_argument("--grid", nargs=2, type=int, default=[512, 288])
    parser.add_argument("--modes", nargs="+", choices=MODES, default=list(MODES))
    parser.add_argument("--aggregations", nargs="+", choices=AGGREGATIONS, default=list(AGGREGATIONS))
    parser.add_argument("--epsilon", nargs="+", type=float)
    args = parser.parse_args()
    epsilons = np.asarray(args.epsilon if args.epsilon is not None else np.geomspace(.00025, 2, 100))
    if not np.isfinite(epsilons).all() or np.any(epsilons <= 0) or min(args.grid) < 1:
        parser.error("Finite positive epsilon/grid required")
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edits matched")
    if args.phase_reference is not None:
        candidate = json.loads(args.phase_reference.read_text(encoding="utf-8"))["candidates"][0]
        report = phases(paths, candidate)
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
