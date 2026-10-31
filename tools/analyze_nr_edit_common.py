"""Study a shared CNN/transformer score; fitted still-frame data, not a temporal guarantee.

Requires NumPy. Labels/model/game come from filenames. Raw pixels and absolute
source paths are never exported. Coefficients are fixed for both model families.
"""

import argparse
import csv
import glob
import hashlib
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import load_edit


THRESHOLDS = {"cnn": .052, "transformer": .084}


def measure(rgb):
    positive = np.maximum(np.asarray(rgb, dtype=np.float64), 0)
    lo = positive.min(axis=-1)
    hi = positive.max(axis=-1)
    # Sort the three channels explicitly: summation cancellation would create
    # false tiny positives when exactly one very bright channel is nonzero.
    middle = np.maximum(np.minimum(positive[..., 0], positive[..., 1]),
                        np.minimum(np.maximum(positive[..., 0], positive[..., 1]), positive[..., 2]))
    means = positive.reshape(-1, 3).mean(axis=0)
    low_mean, middle_mean, high_mean = sorted(means)
    a = float(hi.mean())
    b = float(middle.mean())
    c = float(lo.mean())
    fraction = float(np.mean(middle > 0))
    conditional_middle = b / fraction if fraction else 0.
    terms = dict(amplitude=a,
                 pixel_middle_chroma=max(0., b - c),
                 mean_chroma=max(0., high_mean - low_mean),
                 mean_lower_chroma=max(0., middle_mean - low_mean),
                 occupancy_correction=max(0., conditional_middle - b))
    score = (.125 * terms["amplitude"] + 2 * terms["pixel_middle_chroma"]
             + 1.625 * terms["mean_chroma"] + terms["mean_lower_chroma"]
             + terms["occupancy_correction"])
    return dict(score=score, **terms, positive_rgb_mean=means.tolist(),
                pixel_min_mean=c, pixel_middle_mean=b, two_channel_fraction=fraction)


def analyse(paths):
    rows = []
    for path in paths:
        rgb = load_edit(path)
        model = path.stem.split("-")[1]
        if model not in THRESHOLDS or "ghosting" not in path.stem:
            raise ValueError(f"Unsupported model/label in filename: {path.name}")
        ghost = "noghosting" not in path.stem
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        h, w = rgb.shape[:2]
        row = dict(file=path.name, model=model, game=path.stem.split("-")[0], ghosting=ghost,
                   width=w, height=h, sha256=digest, threshold=THRESHOLDS[model],
                   full=measure(rgb), sampling={})
        for nx, ny in ((32, 18), (64, 36), (128, 72)):
            phases = []
            for py in range(8):
                for px in range(8):
                    x = ((np.arange(nx) * 16 + px * 2 + 1) * w) // (nx * 16)
                    y = ((np.arange(ny) * 16 + py * 2 + 1) * h) // (ny * 16)
                    phases.append(measure(rgb[y[:, None], x])["score"])
            row["sampling"][f"{nx}x{ny}"] = dict(
                min=min(phases), max=max(phases), phases=phases,
                correct=int(sum((v < THRESHOLDS[model]) == ghost for v in phases)))
        rows.append(row)
        print(f"{path.name}: score={row['full']['score']:.8f}, "
              f"64x36 correct={row['sampling']['64x36']['correct']}/64", flush=True)
    holdout = []
    for model in THRESHOLDS:
        for game in sorted({r["game"] for r in rows}):
            train = [r for r in rows if r["model"] == model and r["game"] != game]
            test = [r for r in rows if r["model"] == model and r["game"] == game]
            if not train or not test or {r["ghosting"] for r in train} != {True, False}:
                continue
            threshold = (max(r["full"]["score"] for r in train if r["ghosting"])
                         + min(r["full"]["score"] for r in train if not r["ghosting"])) / 2
            holdout.append(dict(model=model, test_game=game, threshold=threshold,
                                correct=int(sum((r["full"]["score"] < threshold) == r["ghosting"] for r in test)),
                                count=len(test)))
    return dict(format_version=1,
                warning="Post-hoc score selection on these same images and sampling phases; no unseen temporal validation.",
                thresholds=THRESHOLDS, coefficients=[.125, 2., 1.625, 1., 1.],
                files=rows, threshold_game_holdout=holdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="DDS paths or quoted glob patterns")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    paths = sorted({Path(p) for pattern in args.files for p in glob.glob(pattern)})
    if not paths:
        parser.error("No DDS files matched")
    report = analyse(paths)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    keys = ["file", "model", "game", "ghosting", "score", "threshold", "64x36_min", "64x36_max", "64x36_correct"]
    with args.output.with_suffix(".csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, keys)
        writer.writeheader()
        for row in report["files"]:
            sparse = row["sampling"]["64x36"]
            writer.writerow({**{k: row[k] for k in ("file", "model", "game", "ghosting", "threshold")},
                             "score": row["full"]["score"], "64x36_min": sparse["min"],
                             "64x36_max": sparse["max"], "64x36_correct": sparse["correct"]})


if __name__ == "__main__":
    main()
