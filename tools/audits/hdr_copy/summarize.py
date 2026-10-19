"""Summarize audit CSV: positive A-B means the no-copy prototype is faster."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median, quantiles


def summarize(path):
    groups = defaultdict(list)
    with path.open(encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source):
            key = row["series"], row["format"], int(row["mapping"]), int(row["scale"])
            groups[key].append(row)
    summaries = []
    for key, rows in sorted(groups.items()):
        variants = defaultdict(list)
        quartets = defaultdict(lambda: defaultdict(list))
        for row in rows:
            value = float(row["gpu_ms"])
            variant = row["variant"]
            variants[variant].append(value)
            quartets[int(row["window"]) // 4, int(row["sample"])][variant].append(value)
        if any(len(q["base"]) != 2 or len(q["candidate"]) != 2 for q in quartets.values()):
            raise ValueError(f"Incomplete ABBA quartets: {key}")
        deltas = [(sum(q["base"]) - sum(q["candidate"])) / 2 for q in quartets.values()]
        q1, _, q3 = quantiles(deltas, n=4, method="inclusive")
        a, b = median(variants["base"]), median(variants["candidate"])
        summaries.append(dict(zip(("series", "format", "mapping", "scale"), key),
                              samples_per_variant=len(variants["base"]),
                              base_ms=a, candidate_ms=b, median_difference_ms=a-b,
                              paired_delta_ms=median(deltas), paired_iqr_ms=q3-q1))
    return summaries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("summary", type=Path)
    args = parser.parse_args()
    values = summarize(args.samples)
    with args.summary.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(values[0]))
        writer.writeheader()
        writer.writerows(values)
    print(f"Summarized {len(values)} scenario/series groups: {args.summary}")
