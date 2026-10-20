"""Summarize offline probe CSVs with two A and two B measurements per quartet."""
import argparse
import csv
from pathlib import Path
import statistics as stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    summary = []
    fields = ("profile", "source_format", "protection", "scale", "stage")
    for path in args.files:
        groups = {}
        with path.open(encoding="utf-8", newline="") as stream:
            for row in csv.DictReader(stream):
                key = tuple(row.get(f, "") for f in fields) + (int(row["window"]) // 4, int(row["sample"]))
                groups.setdefault(key, {}).setdefault(row["variant"], []).append(float(row["gpu_ms"]))
        print(path.name)
        for settings in sorted({k[:len(fields)] for k in groups}):
            pairs = [v for k, v in groups.items() if k[:len(fields)] == settings]
            if any(set(v) != {"base", "candidate"} or any(len(a) != 2 for a in v.values()) for v in pairs):
                raise ValueError("Incomplete quartet")
            before = [stats.mean(v["base"]) for v in pairs]
            after = [stats.mean(v["candidate"]) for v in pairs]
            delta = [a-b for a, b in zip(before, after)]
            q1, _, q3 = stats.quantiles(delta, n=4, method="inclusive")
            result = dict(zip(fields, settings))
            result.update(file=path.name, quartets=len(pairs), before_ms=stats.median(before),
                          after_ms=stats.median(after), gain_ms=stats.median(delta), gain_iqr_ms=q3-q1)
            summary.append(result)
            print(f"  {settings}: gain {result['gain_ms']:+.6f} ms, IQR {q3-q1:.6f} ms")
    if args.out:
        with args.out.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["file", *fields, "quartets", "before_ms", "after_ms",
                                                        "gain_ms", "gain_iqr_ms"])
            writer.writeheader()
            writer.writerows(summary)


if __name__ == "__main__":
    main()
