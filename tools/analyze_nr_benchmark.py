"""Audit raw NR timings and emit a reproducible Markdown report (no GPU claims)."""
import argparse
import csv
import hashlib
import sys
from pathlib import Path
import numpy as np


def analyze(summary, raw, output):
    version = summary.name.split("-")[1]
    assert version in ("v12", "v13"), version
    experiments = ("v12", "v13") if version == "v13" else ("v12",)
    rows = list(csv.DictReader(summary.open(encoding="utf-8-sig")))
    samples = {}
    for row in csv.DictReader(raw.open(encoding="utf-8-sig")):
        key = (int(row["scale_percent"]), row["variant"])
        samples.setdefault(key, []).append((int(row["sample_index"]), float(row["total_nr_gpu_ms"])))
    assert len(rows) == len(samples)
    lines = [f"# Analiza benchmarku NR {version}", "",
             "RTX 4090, The Last of Us Part I, native 3840×2160; P50 1920×1080, P65 2496×1404; 6 passów, Area, radius 1, guide strength 1.0. 90 warmup + 160 pomiarów na konfigurację. Jeden sekwencyjny sweep. Czas całego przedziału GPU NR, obejmujący NGX, inter-pass i zależności kolejek.", "",
             f"Zweryfikowano {len(rows)} konfiguracji i {sum(len(v) for v in samples.values())} próbek; indeksy bez luk i duplikatów. Metryki podsumowania zgodne z raw do 0,000006 ms. SD populacyjne, P5/P95 nearest-rank, trimmed mean po odrzuceniu 10% z każdego końca. Trend: średnia ostatnich 40 minus pierwszych 40 próbek. Autokorelacja lag-1.", ""]
    stats = {}
    for row in rows:
        key = (int(row["scale_percent"]), row["variant"])
        indexed = samples[key]
        assert [i for i, _ in indexed] == list(range(int(row["samples"])))
        x = np.array([v for _, v in indexed])
        assert len(x) == 160 and int(row["effective_passes"]) == 6
        assert (int(row["native_width"]), int(row["native_height"])) == (3840, 2160)
        assert (int(row["work_width"]), int(row["work_height"])) == ((1920,1080) if key[0] == 50 else (2496,1404))
        s = np.sort(x)
        d = dict(mean_ms=x.mean(), median_ms=np.median(x), p95_ms=s[int(np.ceil(.95*len(x)))-1],
                 min_ms=s[0], max_ms=s[-1], stddev_ms=x.std(), p5=s[int(np.ceil(.05*len(x)))-1],
                 trim=s[16:-16].mean(), lag=np.corrcoef(x[:-1],x[1:])[0,1], trend=x[-40:].mean()-x[:40].mean())
        for name in ("mean_ms","median_ms","p95_ms","min_ms","max_ms","stddev_ms"):
            assert abs(float(row[name])-d[name]) < .000006, (key,name)
        stats[key] = (x,d)
    for scale in (50,65):
        lines += [f"## P{scale}", "", "| Wariant | Mean | Median | P5 | P95 | Min | Max | SD | Trim10% | Δ off | Lag1 | Trend |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        baseline = stats[(scale,"No inter-pass")][1]["median_ms"]
        for (p,name),(x,d) in stats.items():
            if p != scale: continue
            values = [d[k] for k in ("mean_ms","median_ms","p5","p95_ms","min_ms","max_ms","stddev_ms","trim")]
            values += [d["median_ms"]-baseline,d["lag"],d["trend"]]
            lines.append("| "+name+" | "+" | ".join(f"{v:.5f}" for v in values)+" |")
        lines.append("")
    refname = "Linear 16 + isolated weights v10"
    ref,rd = stats[(65,refname)]
    linear = stats[(65,"Tiled linear 16")][1]
    lines += ["## Porównanie " + ", ".join(experiments), "", "Δ = wariant minus referencja; ujemna wartość oznacza krótszy czas.", "", "| Wariant | Δ median v10 | Δ mean v10 | Δ P95 v10 | Δ median Linear16 | Δ mean Linear16 | Δ P95 Linear16 | SD/v10 |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    rng = np.random.default_rng(1213)
    def bootstrap(x, block):
        starts = rng.integers(0,len(x),(10000,int(np.ceil(len(x)/block))))
        idx = (starts[:,:,None]+np.arange(block))%len(x)
        return np.median(x[idx.reshape(10000,-1)[:,:len(x)]],axis=1)
    boot = []
    for (scale,name),(x,d) in stats.items():
        if scale != 65 or not any(v in name for v in experiments): continue
        vals = [d[k]-rd[k] for k in ("median_ms","mean_ms","p95_ms")]
        vals += [d[k]-linear[k] for k in ("median_ms","mean_ms","p95_ms")]
        vals += [d["stddev_ms"]/rd["stddev_ms"]]
        lines.append("| "+name+" | "+" | ".join(f"{v:.5f}" for v in vals)+" |")
        for block in (8,16,32):
            delta = bootstrap(x,block)-bootstrap(ref,block)
            lo,hi = np.quantile(delta,[.025,.975])
            boot.append(f"- {name}, block {block}: Δ median 95% CI [{lo:.5f}, {hi:.5f}] ms.")
    lines += ["", "## Niepewność", "", "Circular moving-block bootstrap: 10000 niezależnych resamplingów na wariant, seed 1213. Przedziały opisują zmienność wewnątrz dostarczonego sweepu. Nie obejmują dryfu między konfiguracjami, temperatury/taktowania, zmian sceny ani różnic między sesjami.", ""] + boot
    lines += ["", "## Przebieg w czasie", "", "Średnie kolejnych bloków po 40 próbek; ms. Indeksy są lokalne dla konfiguracji, bez absolutnego czasu klatki.", "", "| Wariant | 0–39 | 40–79 | 80–119 | 120–159 |", "|---|---:|---:|---:|---:|"]
    for (scale,name),(x,d) in stats.items():
        if scale == 65 and (any(v in name for v in experiments) or name in ("No inter-pass", refname, "Tiled linear 16")):
            lines.append("| "+name+" | "+" | ".join(f"{x[i:i+40].mean():.5f}" for i in range(0,160,40))+" |")
    lines += ["", "## Pochodzenie", ""]
    for path in (summary,raw,summary.with_suffix('.txt')):
        lines.append(f"- {path.name}: SHA-256 `{hashlib.sha256(path.read_bytes()).hexdigest()}`")
    output.write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("summary",type=Path)
    parser.add_argument("raw",type=Path)
    parser.add_argument("output",type=Path)
    args = parser.parse_args()
    analyze(args.summary,args.raw,args.output)
