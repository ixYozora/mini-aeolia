#!/usr/bin/env python3
"""plot_t2.py — coordinated-scheduling tail latency (T2).

Bar chart of LC-thread tail latency (median, p99, p99.9, p99.99) per scheduler,
log scale. Shows the coordinated scheduler taming the tail under contention.
"""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t2_coexist.csv")

LABELS = {"default": "default\n(EEVDF)", "scx_fifo": "scx_fifo\n(no coord)",
          "scx_coord": "scx_coord\n(LC preempts)"}
PCTS = [("median_ns", "median"), ("p99_ns", "p99"),
        ("p999_ns", "p99.9"), ("p9999_ns", "p99.99")]

def main():
    if not os.path.exists(CSV): sys.exit(f"no data at {CSV}; run scripts/run_t2.sh")
    rows = {r["config"]: r for r in csv.DictReader(open(CSV))}
    order = [c for c in ["default", "scx_fifo", "scx_coord"] if c in rows]

    x = np.arange(len(order)); w = 0.2
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for i, (key, name) in enumerate(PCTS):
        ys = [float(rows[c][key]) / 1000.0 for c in order]  # us
        ax.bar(x + (i - 1.5)*w, ys, w, label=name)
    ax.set_xticks(x); ax.set_xticklabels([LABELS.get(c, c) for c in order])
    ax.set_ylabel("LC read latency (µs)"); ax.set_yscale("log")
    ax.set_title("T2: coordinated scheduling tames tail latency under contention\n"
                 "(1 latency-critical I/O thread + 3 compute hogs on one core)")
    ax.legend(title="percentile", ncol=4)
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t2_tail_latency.png")
    fig.savefig(out, dpi=130); print("wrote", out)

    # also print the headline ratio
    if "default" in rows and "scx_coord" in rows:
        d = float(rows["default"]["p999_ns"]); c = float(rows["scx_coord"]["p999_ns"])
        print(f"p99.9 improvement (default -> scx_coord): {d/c:.1f}x")

if __name__ == "__main__":
    main()
