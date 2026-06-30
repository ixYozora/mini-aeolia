#!/usr/bin/env python3
"""T6 — LevelDB db_bench: ext4 vs f2fs (paper Table 7/8 baseline columns)."""
import os, csv, sys
from collections import defaultdict
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t6_leveldb.csv")

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_leveldb.sh")
    data = defaultdict(dict)
    benches = []
    for r in csv.DictReader(open(CSV)):
        data[r["fs"]][r["bench"]] = float(r["ops_per_s"])
        if r["bench"] not in benches: benches.append(r["bench"])
    fss = [f for f in ("ext4", "f2fs") if f in data]
    colors = {"ext4": "#264653", "f2fs": "#e9c46a"}
    x = np.arange(len(benches)); w = 0.8 / len(fss)
    fig, ax = plt.subplots(figsize=(8, 4.2))
    for i, f in enumerate(fss):
        off = (i - (len(fss)-1)/2) * w
        ax.bar(x + off, [data[f].get(b, 0) for b in benches], w, label=f, color=colors[f])
    ax.set_xticks(x); ax.set_xticklabels(benches, rotation=15)
    ax.set_ylabel("ops / sec"); ax.set_yscale("log")
    ax.set_title("T6: LevelDB db_bench — ext4 vs f2fs (paper Table 8 baselines)\n"
                 "ext4 leads most; f2fs wins fillsync (log-structured). AeoFS column needs UINTR/MPK.")
    ax.legend()
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t6_leveldb.png"); fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__":
    main()
