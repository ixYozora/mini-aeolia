#!/usr/bin/env python3
"""T3b — effect of the mfs buffer cache: nocache vs cache vs ext4 (ops/sec)."""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t3b_cache.csv")

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_t3_cache.sh")
    rows = list(csv.DictReader(open(CSV)))
    ops = ["create", "write", "read", "stat"]
    order = [t for t in ["mfs_nocache", "mfs_cache", "ext4", "f2fs"]
             if any(r["target"] == t for r in rows)]
    labels = {"mfs_nocache": "mfs (no cache)", "mfs_cache": "mfs (cache)",
              "ext4": "ext4", "f2fs": "f2fs"}
    colors = {"mfs_nocache": "#e76f51", "mfs_cache": "#2a9d8f",
              "ext4": "#264653", "f2fs": "#e9c46a"}
    data = {t: {r["op"]: float(r["ops_per_s"]) for r in rows if r["target"] == t} for t in order}

    x = np.arange(len(ops)); w = 0.8 / len(order)
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for i, t in enumerate(order):
        off = (i - (len(order)-1)/2) * w
        ax.bar(x + off, [data[t].get(o, 0) for o in ops], w, label=labels[t], color=colors[t])
    ax.set_xticks(x); ax.set_xticklabels(ops)
    ax.set_ylabel("ops / sec  (higher = faster)"); ax.set_yscale("log")
    ax.set_title("T3b: the buffer cache makes mfs beat ext4 on create/read/stat")
    ax.legend()
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t3b_cache.png"); fig.savefig(out, dpi=130)
    print("wrote", out)
    # ratios
    for o in ops:
        c, e = data["mfs_cache"].get(o), data["ext4"].get(o)
        if c and e: print(f"{o:7s}: mfs_cache/ext4 = {c/e:.2f}x")

if __name__ == "__main__":
    main()
