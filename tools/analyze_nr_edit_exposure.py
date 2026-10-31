"""Apply the current OptiScaler image-based exposure meters to captured NR edits.

Reconstructs the plain residual carrier (neutral 0.5) and its real FP16 store.
Static per-frame readings only: no game exposure, NGX internal readback, adaptation
history, auto exposure scan or manual setting can be recovered from these DDS.
Requires NumPy. CPU reference, checked separately against production GPU shaders.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np

from analyze_nr_edit_ghosting import edit_paths, load_edit

PRIVATE_WEIGHTS = np.array([.25, .5, .25])
METER_WEIGHTS = np.array([.2126, .7152, .0722])
METHODS = ("legacy_cnn", "legacy_k", "transformer2", "average_0", "average_50", "average_100",
           "percentile_0_100", "percentile_10_90", "percentile_25_75")


def carrier(edit, ratio=1.):
    scaled = np.minimum(edit, 0) + np.maximum(edit, 0) * np.float32(ratio)
    return np.clip(np.float32(.5) + scaled, 0, 65504).astype(np.float16).astype(np.float32)


def half_rtz(value):
    if not np.isfinite(value) or value <= 0:
        return 1.
    code = np.float16(value).view(np.uint16)
    if float(code.view(np.float16)) > value:
        code = np.uint16(code - 1)
    result = float(code.view(np.float16))
    return result if np.isfinite(result) and result > 0 else 1.


def private_samples(colour):
    height, width = colour.shape[:2]
    sw, sh = max(2, width // 2), max(2, height // 2)
    x = np.floor(np.arange(sw) / (sw - 1) * (width - 1) + .5).astype(int)
    y = np.floor(np.arange(sh) / (sh - 1) * (height - 1) + .5).astype(int)
    return colour[y[:, None], x]


def tile_means(luma):
    height, width = luma.shape
    if width < 64 or height < 64:
        raise ValueError("This study requires at least 64x64")
    integral = np.pad(luma, ((1, 0), (1, 0)))
    np.cumsum(integral, axis=0, out=integral)
    np.cumsum(integral, axis=1, out=integral)
    x, y = np.arange(65) * width // 64, np.arange(65) * height // 64
    sums = (integral[y[1:, None], x[None, 1:]] - integral[y[:-1, None], x[None, 1:]]
            - integral[y[1:, None], x[None, :-1]] + integral[y[:-1, None], x[None, :-1]])
    pixels = np.diff(y)[:, None] * np.diff(x)[None, :]
    return (sums / pixels).ravel(), pixels.ravel()


def percentile_luma(tiles, black, low, high):
    values = tiles[tiles > black]
    if len(values) < .05 * 4096:
        return None
    ev = np.clip(np.log2(np.maximum(values, 1e-8)), -24, 24)
    bins = np.minimum(((ev + 24) * (64 / 48)).astype(int), 63)
    counts = np.bincount(bins, minlength=64)
    fixed = np.floor((ev + 24) * 1024 + .5).astype(np.uint32)
    sums = np.bincount(bins, weights=fixed, minlength=64)
    lo, hi = len(values) * low / 100, len(values) * high / 100
    if hi - lo < 1:
        lo, hi = 0, len(values)
    cumulative = np.r_[0, np.cumsum(counts)[:-1]]
    weight = np.maximum(np.minimum(cumulative + counts, hi) - np.maximum(cumulative, lo), 0)
    means = np.divide(sums, 1024 * counts, out=np.zeros(64), where=counts > 0) - 24
    return float(np.exp2(np.sum(means * weight) / np.sum(weight)))


def private_meter(private_y):
    logs = np.zeros_like(private_y)
    valid = (private_y > 0) & np.isfinite(private_y)
    logs[valid] = np.log(private_y[valid] + 1e-6)
    legacy = half_rtz((.18 / .82) * np.exp(-logs.mean()))
    t2 = half_rtz(.23547087609767914 / max(float(np.clip(private_y, 0, 10000).mean()), .005))
    return dict(legacy_cnn=legacy, legacy_k=legacy, transformer2=t2)


def automatic_meter(y):
    result = {}
    tiles, pixels = tile_means(y)
    plain = float(np.average(tiles, weights=pixels))
    core = tiles <= plain * 16
    core_mean = float(np.average(tiles[core], weights=pixels[core])) if np.any(core) else plain
    black = core_mean * 2 ** -12
    selected = tiles > black
    if pixels[selected].sum() < .05 * pixels.sum():
        result.update({key: None for key in METHODS[3:]})
        return result
    values, weights = tiles[selected], pixels[selected]
    for protection in (0, 50, 100):
        measured = float(np.average(values, weights=weights))
        if protection:
            ev = np.clip(np.log2(np.maximum(values, 1e-8)), -24, 24)
            reference = float(np.average(ev, weights=weights))
            p = protection / 100
            knee, slope = 3 - 2 * p, 1 - .65 * p
            compressed = np.where(ev - reference > knee,
                                  reference + knee + (ev - reference - knee) * slope, ev)
            measured = float(np.average(np.exp2(np.clip(compressed, -24, 24)), weights=weights))
        result[f"average_{protection}"] = .18 / (.82 * measured) if measured > 1e-8 else None
    for low, high in ((0, 100), (10, 90), (25, 75)):
        measured = percentile_luma(tiles, black, low, high)
        result[f"percentile_{low}_{high}"] = .18 / (.82 * measured) if measured and measured > 1e-8 else None
    return result


def meter(colour, channel=None):
    # Replicated R/G/B views are supplemental; shipped meters produce one RGB reading.
    sampled = private_samples(colour).astype(np.float64)
    private_y = sampled @ PRIVATE_WEIGHTS if channel is None else sampled[..., channel]
    y = colour.astype(np.float64) @ METER_WEIGHTS if channel is None else colour[..., channel].astype(np.float64)
    return dict(**private_meter(private_y), **automatic_meter(y))


def separation(rows, method):
    clear = [r[method] for r in rows if not r["ghosting"]]
    bad = [r[method] for r in rows if r["ghosting"]]
    return dict(min_clear=min(clear), max_clear=max(clear), min_bad=min(bad), max_bad=max(bad),
        lower_exposure_separates=max(clear) < min(bad), higher_exposure_separates=min(clear) > max(bad),
        one_sided_low_target=min(clear + bad), boundary_ratio=max(clear) / min(clear))


def gain_audit(paths, rows):
    targets = {}
    for model in ("cnn", "transformer"):
        group = [r for r in rows if r["model"] == model and r["view"] == "rgb"]
        targets[model] = {}
        for method in METHODS:
            clear = [r[method] for r in group if not r["ghosting"]]
            bad = [r[method] for r in group if r["ghosting"]]
            targets[model][method] = min(min(clear), float(np.nextafter(min(bad), -np.inf)))
    output = []
    for path in paths:
        if "noghosting" not in path.name:
            continue
        edit = load_edit(path)
        sampled_edit = private_samples(edit)
        model = path.stem.split("-")[1]
        cache = {"private": {}, "automatic": {}}
        def value(method, ratio):
            private = method in METHODS[:3]
            group = "private" if private else "automatic"
            if ratio not in cache[group]:
                if private:
                    c = carrier(sampled_edit, ratio).astype(np.float64)
                    cache[group][ratio] = private_meter(c @ PRIVATE_WEIGHTS)
                else:
                    c = carrier(edit, ratio).astype(np.float64)
                    cache[group][ratio] = automatic_meter(c @ METER_WEIGHTS)
            return cache[group][ratio][method]
        results = {}
        for method in METHODS:
            target = targets[model][method]
            low, high = 1., 1.
            while high < 128 and value(method, high) > target:
                low, high = high, high * 2
            if value(method, high) > target:
                results[method] = None
                continue
            for _ in range(18):
                mid = (low + high) / 2
                if value(method, mid) <= target:
                    high = mid
                else:
                    low = mid
            results[method] = high
        monotonic = {}
        for method in METHODS:
            group = "private" if method in METHODS[:3] else "automatic"
            pairs = sorted((ratio, values[method]) for ratio, values in cache[group].items())
            monotonic[method] = all(b[1] <= a[1] + 1e-6 for a, b in zip(pairs, pairs[1:]))
        output.append(dict(file=path.name, model=model, relative_gain=results, sampled_curve_nonincreasing=monotonic))
        print(f"{path.name}: exposure-based gain audit complete", flush=True)
    return dict(warning="Counterfactual CPU carrier sweep, only positive edit scaled. First bracket up to 128, then 18 bisections. Not a temporal/no-ghosting guarantee.",
                targets=targets, files=output)


def validate_gpu(rows, csv_path, private_only=False):
    with csv_path.open(newline="", encoding="utf-8") as stream:
        gpu = list(csv.DictReader(stream))
    expected = {(r["file"], r["view"]): r for r in rows}
    actual = {(r["file"], r["view"]): r for r in gpu}
    if len(actual) != len(gpu) or actual.keys() != expected.keys():
        raise AssertionError("GPU/CPU source sets differ or contain duplicate records")
    methods = METHODS[:3] if private_only else METHODS
    errors = {method: 0. for method in methods}
    exact_private = 0
    for identity, row in actual.items():
        reference = expected[identity]
        for method in methods:
            value, wanted = float(row[method]), reference[method]
            if not np.isfinite(value) or value <= 0 or wanted is None:
                raise AssertionError(f"Invalid reading: {identity}/{method}")
            relative = abs(value - wanted) / wanted
            errors[method] = max(errors[method], relative)
            private = method in METHODS[:3]
            if private and np.float32(value) == np.float32(wanted):
                exact_private += 1
            # Same private one-half-interval allowance as private_exposure_quality.
            # Main meters use an independent double reference with integer histogram sums.
            limit = .0011 if private else 1e-5
            if relative > limit:
                raise AssertionError(f"GPU/reference mismatch: {identity}/{method}: {value} vs {wanted}")
    return dict(rows=len(actual), values=len(actual) * len(methods),
                private_values_exact_as_float32=exact_private, max_relative_error=errors)


def derived_features(rows):
    by_file = {}
    for row in rows:
        by_file.setdefault(row["file"], {})[row["view"]] = row
    if any(set(views) != {"rgb", "r", "g", "b"} for views in by_file.values()):
        return dict(warning="Supplemental R/G/B views required")
    features = []
    for filename, views in by_file.items():
        scores = {}
        for method in (m for m in METHODS if m not in ("legacy_k", "average_50")):
            values = np.array([views[c][method] for c in ("r", "g", "b")])
            for name, value in (("min", values.min()), ("median", np.median(values)),
                                ("max", values.max()), ("mean", values.mean()),
                                ("span_ev", np.log2(values.max() / values.min()))):
                scores[f"channels/{method}/{name}"] = float(value)
        per_view = {}
        for view, row in views.items():
            geometric = (.18 / .82) / row["legacy_cnn"]
            arithmetic = .23547087609767914 / row["transformer2"]
            per_view[view] = dict(am_gm_ratio=arithmetic / geometric, am_minus_gm=arithmetic - geometric,
                average_pct_ratio=row["percentile_10_90"] / row["average_0"],
                average_pct_difference=row["percentile_10_90"] - row["average_0"],
                average_log_ratio=row["percentile_0_100"] / row["average_0"])
            scores.update({f"{view}/{key}": float(value) for key, value in per_view[view].items()})
        for feature in per_view["rgb"]:
            values = np.array([per_view[c][feature] for c in ("r", "g", "b")])
            for name, value in (("min", values.min()), ("median", np.median(values)),
                                ("mean", values.mean()), ("max", values.max()),
                                ("rms", np.sqrt(np.mean(values * values)))):
                scores[f"channels/{feature}/{name}"] = float(value)
        features.append(dict(file=filename, model=views["rgb"]["model"], ghosting=views["rgb"]["ghosting"], scores=scores))
    summary = {}
    for model in ("cnn", "transformer"):
        group = [r for r in features if r["model"] == model]
        summary[model] = {}
        for key in features[0]["scores"]:
            clear = [r["scores"][key] for r in group if not r["ghosting"]]
            bad = [r["scores"][key] for r in group if r["ghosting"]]
            summary[model][key] = dict(clear_range=[min(clear), max(clear)], bad_range=[min(bad), max(bad)],
                separates=max(clear) < min(bad) or min(clear) > max(bad))
    common = [key for key in summary["cnn"] if summary["cnn"][key]["separates"] and summary["transformer"][key]["separates"]]
    return dict(warning="Explicit exploratory transforms, no fitted weights. Selection still uses the same labels, not a holdout.",
                files=features, summary=summary, shared_separating_features=common)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rgb-only", action="store_true")
    parser.add_argument("--gpu-csv", type=Path)
    parser.add_argument("--warp-csv", type=Path)
    parser.add_argument("--gpu-label", default="unspecified", help="Device/driver annotation for externally produced GPU CSV")
    parser.add_argument("--gain-audit", action="store_true", help="Also audit positive-only gain to a first-clear exposure target")
    args = parser.parse_args()
    paths = edit_paths(args.files)
    if not paths:
        parser.error("No raw edit DDS matched")
    rows = []
    for path in paths:
        edit = load_edit(path)
        colour = carrier(edit)
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        for view, channel in (("rgb", None), ("r", 0), ("g", 1), ("b", 2)):
            if args.rgb_only and channel is not None:
                continue
            rows.append(dict(file=path.name, model=path.stem.split("-")[1], game=path.stem.split("-")[0],
                ghosting="noghosting" not in path.stem, sha256=digest, view=view,
                width=colour.shape[1], height=colour.shape[0], **meter(colour, channel)))
        print(f"{path.name}: exposure complete", flush=True)
    summary = {model: {view: {method: separation([r for r in rows if r['model'] == model and r['view'] == view], method)
                             for method in METHODS} for view in sorted({r['view'] for r in rows})}
               for model in ("cnn", "transformer")}
    report = dict(warning="Static same-data study, no temporal guarantee or automatic gain controller.",
        input="FP16(clamp(0.5 + captured_signed_edit, 0, 65504)); PreExposure=1, linear input, saturation identity/depth colour off",
        private_grid="floor(width/2) x floor(height/2), endpoint nearest; render=output in private DLAA",
        temporal_scope="Fresh measured frame; no history/adaptation from unknown previous frames",
        non_image_modes="Native NGX internals, game/scan exposure, manual/paper-white and adaptation history not recoverable from DDS",
        files=rows, separation=summary)
    root = Path(__file__).resolve().parents[1]
    sources = ("OptiScaler/shaders/dlssnr/superior/PrivateExposure.hlsl",
               "OptiScaler/shaders/dlssnr/superior/precompile/private_exposure_Shader.cso",
               "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl",
               "OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.cso")
    report["code_identity"] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources}
    report["gpu_device"] = args.gpu_label
    report["unit_mode"] = dict(exposure=1.0, note="Constant, not an image meter; no separation of labels")
    report["derived_features"] = derived_features(rows)
    if args.gpu_csv:
        report["gpu_validation"] = validate_gpu(rows, args.gpu_csv)
    if args.warp_csv:
        report["warp_private_validation"] = validate_gpu(rows, args.warp_csv, True)
        report["warp_scope"] = "Private exposure PSO only; main NR percentile PSO excluded because of existing WARP failure"
    if args.gain_audit:
        report["gain_audit"] = gain_audit(paths, rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    with args.output.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    for model in summary:
        for method in METHODS:
            s = summary[model]["rgb"][method]
            print(model, method, f"clear={s['min_clear']:.8f}..{s['max_clear']:.8f}",
                  f"bad={s['min_bad']:.8f}..{s['max_bad']:.8f}",
                  f"separates={s['lower_exposure_separates'] or s['higher_exposure_separates']}")


if __name__ == "__main__":
    main()
