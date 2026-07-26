#!/usr/bin/env python3
"""plot_t3.py — mini-libFS vs ext4 micro-benchmark figure (ops/sec per op)."""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t3_fs_micro.csv")

def main():
    if not os.path.exists(CSV): sys.exit(f"no data at {CSV}; run scripts/run_t3.sh")
    rows = list(csv.DictReader(open(CSV)))
    ops = ["create", "write", "read", "stat"]
    targets = sorted({r["target"] for r in rows})
    data = {t: {r["op"]: float(r["ops_per_s"]) for r in rows if r["target"] == t} for t in targets}

    x = np.arange(len(ops)); w = 0.8 / max(1, len(targets))
    fig, ax = plt.subplots(figsize=(7, 4))
    for i, t in enumerate(targets):
        ax.bar(x + (i - (len(targets)-1)/2)*w, [data[t].get(o, 0) for o in ops], w, label=t)
    ax.set_xticks(x); ax.set_xticklabels(ops)
    ax.set_ylabel("ops / sec"); ax.set_yscale("log")
    ax.set_title("T3: mini-libFS vs ext4 (same null_blk device)")
    ax.legend()
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t3_fs_micro.png")
    fig.savefig(out, dpi=130); print("wrote", out)

if __name__ == "__main__":
    main()
