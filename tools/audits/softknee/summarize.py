"""Summarize paired GPU timestamps; positive gain means a faster candidate."""
import argparse
import csv
from pathlib import Path
import statistics as stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.files:
        groups = {}
        with path.open(encoding="utf-8", newline="") as stream:
            for row in csv.DictReader(stream):
                key = row["profile"], row["scale"], int(row["window"]) // 4, int(row["sample"])
                groups.setdefault(key, {}).setdefault(row["variant"], []).append(float(row["gpu_ms"]))
        print(path.name)
        for profile, scale in sorted({key[:2] for key in groups}):
            pairs = [v for k, v in groups.items() if k[:2] == (profile, scale)]
            if any(set(v) != {"base", "candidate"} or any(len(a) != 2 for a in v.values()) for v in pairs):
                raise ValueError("Incomplete ABBA/BAAB quartet")
            before = [stats.mean(v["base"]) for v in pairs]
            after = [stats.mean(v["candidate"]) for v in pairs]
            delta = [a-b for a, b in zip(before, after)]
            q1, _, q3 = stats.quantiles(delta, n=4, method="inclusive")
            print(f"  {profile} P{scale}: n={len(pairs)}, before={stats.median(before):.6f} ms, "
                  f"after={stats.median(after):.6f} ms, paired gain={stats.median(delta):.6f} ms, "
                  f"gain IQR={q3-q1:.6f} ms")


if __name__ == "__main__":
    main()
