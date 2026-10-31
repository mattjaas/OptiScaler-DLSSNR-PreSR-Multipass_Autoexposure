"""Screen coarse contrast on plain, 0.5-centered residual carriers.

Requires NumPy. This is a rejected exploratory study, not a runtime detector.
Box averages are exact; FP16 rounding is omitted. Only positive gain changes.
Labels/model/game come from filenames. No raw pixels or source paths exported.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import edit_paths, load_edit

RATIOS = np.array([.5, .75, 1, 1.02, 1.05, 1.1, 1.15, 1.2, 1.3, 1.5, 2, 3, 4, 8, 16, 32])
GRIDS = ((16, 9), (32, 18), (64, 36), (128, 72))


def integral(values):
    result = np.pad(values, ((1, 0), (1, 0), (0, 0)))
    np.cumsum(result, axis=0, out=result)
    np.cumsum(result, axis=1, out=result)
    return result


def area(summed, nx, ny, width, height):
    x = np.arange(nx + 1) * width // nx
    y = np.arange(ny + 1) * height // ny
    sums = (summed[y[1:, None], x[None, 1:]] - summed[y[:-1, None], x[None, 1:]]
            - summed[y[1:, None], x[None, :-1]] + summed[y[:-1, None], x[None, :-1]])
    return sums / (np.diff(y)[:, None, None] * np.diff(x)[None, :, None])


def stats(colour):
    luma = colour @ np.array([.25, .5, .25])
    result = {}
    for name, values in (("rgb", colour), ("luma", luma)):
        left, right = values[:, :-1], values[:, 1:]
        top, bottom = values[:-1], values[1:]
        relative = np.r_[(np.abs(left - right) / (left + right + 1e-12)).ravel(),
                         (np.abs(top - bottom) / (top + bottom + 1e-12)).ravel()]
        absolute = np.r_[np.abs(left - right).ravel(), np.abs(top - bottom).ravel()]
        for power in (1, 2):
            result[f"{name}_michelson_p{power}"] = float(np.mean(relative ** power) ** (1 / power))
            result[f"{name}_difference_p{power}"] = float(np.mean(absolute ** power) ** (1 / power))
        for quantile in (.5, .75, .9):
            result[f"{name}_michelson_q{quantile}"] = float(np.quantile(relative, quantile))
    result["global_cv"] = float(luma.std() / max(luma.mean(), 1e-12))
    for low, high in ((.1, .9), (.25, .75)):
        a, b = np.quantile(luma, [low, high])
        result[f"global_michelson_{low}_{high}"] = float((b - a) / (b + a + 1e-12))
    def neighbourhood_mean(values):
        padded = np.pad(values, ((1, 1), (1, 1)), mode="edge")
        return sum(padded[y:y + luma.shape[0], x:x + luma.shape[1]]
                   for y in range(3) for x in range(3)) / 9
    mean = neighbourhood_mean(luma)
    mean_square = neighbourhood_mean(luma * luma)
    cv = np.sqrt(np.maximum(0, mean_square - mean * mean)) / np.maximum(mean, 1e-12)
    result["local_cv_mean"] = float(cv.mean())
    result["local_cv_rms"] = float(np.sqrt(np.mean(cv * cv)))
    return result


def screen(rows):
    identity = list(RATIOS).index(1)
    candidates = []
    for key in rows[0]["data"]:
        factors = {}
        for model in ("cnn", "transformer"):
            group = [row for row in rows if row["model"] == model]
            clear = [row for row in group if not row["ghosting"]]
            bad = [row for row in group if row["ghosting"]]
            if not clear or not bad:
                raise ValueError(f"Need both labels for {model}")
            bad_max = max(row["data"][key][identity] for row in bad)
            target = max(max(row["data"][key][identity] for row in clear),
                         np.nextafter(bad_max, np.inf))
            required = []
            for row in clear:
                hits = np.flatnonzero((RATIOS >= 1) & (np.array(row["data"][key]) >= target))
                if not len(hits):
                    break
                required.append(float(RATIOS[hits[0]]))
            if len(required) != len(clear):
                break
            factors[model] = dict(target=target, required_rhos=required, worst=max(required),
                                  bad_max=bad_max, clear_files=[r["file"] for r in clear],
                                  clears=[r["data"][key][identity] for r in clear])
        if len(factors) == 2:
            candidates.append(dict(key=key, worst=max(v["worst"] for v in factors.values()), models=factors))
    return sorted(candidates, key=lambda row: row["worst"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", help="Raw edit DDS paths or quoted globs")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edits matched")
    rows = []
    for path in paths:
        edit = load_edit(path)
        height, width = edit.shape[:2]
        positive = np.maximum(edit.astype(np.float64), 0)
        base = np.maximum(.5 + np.minimum(edit.astype(np.float64), 0), 0)
        if .5 + positive.max() * RATIOS.max() >= 65504:
            raise ValueError("HDR cap requires exact per-pixel reconstruction before box averaging")
        ip, ib = integral(positive), integral(base)
        row = dict(file=path.name, model=path.stem.split("-")[1], game=path.stem.split("-")[0],
                   ghosting="noghosting" not in path.stem, data={})
        for nx, ny in GRIDS:
            a, b = area(ip, nx, ny, width, height), area(ib, nx, ny, width, height)
            for ratio in RATIOS:
                for key, value in stats(b + ratio * a).items():
                    row["data"].setdefault(f"{nx}x{ny}_{key}", []).append(value)
        rows.append(row)
        print(f"{path.name}: coarse contrast complete", flush=True)
    candidates = screen(rows)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    study = dict(warning="Post-hoc coarse screening, no object segmentation, FP16 quantization or temporal DLSS.",
                 rhos=RATIOS.tolist(), files=rows)
    for name, data in (("local-contrast-search.json", study), ("local-contrast-candidates.json", candidates)):
        (args.output_dir / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    if candidates:
        print(f"Best screened worst-case gain ratio: {candidates[0]['worst']} (coarse grid, not exact optimum)")


if __name__ == "__main__":
    main()
