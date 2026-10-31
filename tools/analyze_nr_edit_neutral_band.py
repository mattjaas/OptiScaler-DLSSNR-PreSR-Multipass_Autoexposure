"""Exploratory neutral-band coverage, not a temporal ghosting detector.

Only positive gain is varied. Reconstructs C=max(0.5+min(E,0),0)+rho*max(E,0).
No FP16 rounding or upper cap is modeled (tested points must remain below cap).
Requires NumPy. Labels come from filenames. No pixels/absolute paths exported.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import edit_paths, load_edit

METRICS = ("rgb_mean_abs", "rgb_rms_abs", "luma_positive", "luma_abs", "luma_positive_709", "luma_abs_709",
           "any_channel", "two_channels", "all_channels", "channel_fraction")
WEIGHTS = np.array([.25, .5, .25])


def gain_events(row, metric, epsilon):
    p, n = row["positive"], row["negative"]
    if metric.startswith("luma_"):
        weights = np.array([.2126, .7152, .0722]) if metric.endswith("_709") else WEIGHTS
        a, b = p @ weights, n @ weights
        bright = np.divide(epsilon - b, a, out=np.full(a.shape, np.inf), where=a > 0)
        if metric.startswith("luma_abs"):
            dark = np.divide(-epsilon - b, a, out=np.full(a.shape, -np.inf), where=a > 0)
            fixed_dark = int(np.count_nonzero((a == 0) & (b < -epsilon)))
            return bright, dark, fixed_dark
        return bright, None, 0
    if metric == "rgb_mean_abs":
        a, b = p.mean(1), -n.mean(1)
        gain = np.divide(epsilon - b, a, out=np.full(a.shape, np.inf), where=a > 0)
        gain[b > epsilon] = -np.inf
        return gain, None, 0
    if metric == "rgb_rms_abs":
        a, b = (p * p).mean(1), (n * n).mean(1)
        gain = np.sqrt(np.maximum(np.divide(epsilon ** 2 - b, a,
                              out=np.full(a.shape, np.inf), where=a > 0), 0))
        gain[b > epsilon ** 2] = -np.inf
        return gain, None, 0
    gain = np.divide(epsilon, p, out=np.full(p.shape, np.inf), where=p > 0)
    gain[-n > epsilon] = -np.inf
    if metric == "any_channel":
        gain = gain.min(1)
    elif metric == "two_channels":
        gain = np.median(gain, axis=1)
    elif metric == "all_channels":
        gain = gain.max(1)
    else:
        gain = gain.ravel()
    return gain, None, 0


def coverage(events):
    bright, dark, fixed = events
    count = int(np.count_nonzero(bright < 1))
    if dark is not None:
        count += int(np.count_nonzero(dark > 1)) + fixed
    return count / len(bright)


def required_gain(events, target):
    bright, dark, fixed = events
    # Strictly beyond every observed first-clear coverage and every bad coverage.
    # Integer rank avoids nextafter(target)*N rounding back onto a plateau.
    required_count = int(np.floor(np.nextafter(target, np.inf) * len(bright))) + 1
    if required_count > len(bright):
        return None
    if dark is None:
        index = required_count - 1
        gain = float(np.partition(bright, index)[index])
    else:
        # Absolute luma is not monotonic: initially dark pixels can enter the
        # neutral band before becoming bright. Sweep both event types exactly.
        base = int(np.count_nonzero(bright <= 1)) + fixed
        starts = np.sort(bright[(bright > 1) & np.isfinite(bright)])
        ends = np.sort(dark[dark > 1])
        starts, multiplicities = np.unique(starts, return_counts=True)
        counts = base + np.cumsum(multiplicities) + len(ends) - np.searchsorted(ends, starts, side="right")
        hits = np.flatnonzero(counts >= required_count)
        gain = float(starts[hits[0]]) if len(hits) else np.inf
    if not np.isfinite(gain):
        return None
    return float(np.nextafter(gain, np.inf))


def screen(rows, metrics, epsilons):
    candidates = []
    for metric in metrics:
        for epsilon in epsilons:
            events = [gain_events(row, metric, epsilon) for row in rows]
            coverages = [coverage(e) for e in events]
            families = {}
            for model in ("cnn", "transformer"):
                indices = [i for i, row in enumerate(rows) if row["model"] == model]
                good = [i for i in indices if not rows[i]["ghosting"]]
                bad = [i for i in indices if rows[i]["ghosting"]]
                if not good or not bad:
                    raise ValueError(f"Need both labels for {model}")
                target = max(coverages[i] for i in indices)
                gains = [required_gain(events[i], target) for i in good]
                if any(g is None or .5 + g * rows[i]["positive_max"] >= 65504 for i, g in zip(good, gains)):
                    break
                if any(g < 1 for g in gains):
                    raise AssertionError("Strict coverage target must not lower a first-clear gain")
                families[model] = dict(target_coverage_lower_bound=target,
                    first_clear_gain_ratios=gains, worst=max(gains),
                    clear_files=[rows[i]["file"] for i in good],
                    clear_coverage=[coverages[i] for i in good], bad_coverage=[coverages[i] for i in bad],
                    labels_separable=max(coverages[i] for i in bad) < min(coverages[i] for i in good))
            if len(families) == 2:
                candidates.append(dict(metric=metric, epsilon=float(epsilon),
                                       worst=max(r["worst"] for r in families.values()), families=families))
        print(f"{metric}: complete", flush=True)
    return sorted(candidates, key=lambda row: row["worst"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="Raw edit DDS paths or quoted globs")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="Use every pixel instead of preliminary dense grid")
    parser.add_argument("--grid", nargs=2, type=int, default=[512, 288])
    parser.add_argument("--metrics", nargs="+", choices=METRICS, default=list(METRICS))
    parser.add_argument("--epsilon", nargs="+", type=float, help="Default: 100 log-spaced values, .00025 to 2")
    args = parser.parse_args()
    epsilons = np.asarray(args.epsilon if args.epsilon is not None else np.geomspace(.00025, 2, 100))
    if not np.isfinite(epsilons).all() or np.any(epsilons <= 0) or min(args.grid) < 1:
        parser.error("Finite positive epsilon/grid required")
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edits matched")
    rows = []
    for path in paths:
        edit = load_edit(path)
        height, width = edit.shape[:2]
        if not args.full:
            nx, ny = args.grid
            x = (np.arange(nx) * 2 + 1) * width // (nx * 2)
            y = (np.arange(ny) * 2 + 1) * height // (ny * 2)
            edit = edit[y[:, None], x]
        edit = np.asarray(edit, dtype=np.float64).reshape(-1, 3)
        rows.append(dict(file=path.name, model=path.stem.split("-")[1],
                         ghosting="noghosting" not in path.stem,
                         positive=np.maximum(edit, 0), negative=np.minimum(np.maximum(edit, -.5), 0),
                         positive_max=float(np.maximum(edit, 0).max())))
        print(f"{path.name}: loaded", flush=True)
    candidates = screen(rows, args.metrics, epsilons)
    report = dict(warning="Post-hoc same-data screening; no FP16 rounding, upper cap or temporal validation.",
                  input_mode="plain residual around 0.5; only positive gain changes",
                  sampling="full" if args.full else args.grid, epsilons=epsilons.tolist(),
                  candidates=candidates)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if candidates:
        best = candidates[0]
        print(f"Best screened: {best['metric']}, epsilon={best['epsilon']}, worst gain ratio={best['worst']}")


if __name__ == "__main__":
    main()
