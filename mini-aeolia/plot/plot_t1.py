#!/usr/bin/env python3
"""plot_t1.py — figures for Test T1 (latency decomposition).

Reads results/t1_latency.csv and produces:
  results/t1_median_latency.png   median latency per mode, grouped by block size
  results/t1_interrupt_gap.png    iou vs iou_poll gap (the "interrupt overhead"),
                                  default vs active_check scheduler if both present

Usage: python3 plot/plot_t1.py
"""
import os
import sys
import csv
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t1_latency.csv")


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            r["bs"] = int(r["bs"])
            r["median_ns"] = float(r["median_ns"])
            r["mean_ns"] = float(r["mean_ns"])
            r["sched"] = r.get("sched", "default")
            rows.append(r)
    return rows


def fig_median(rows):
    scheds = sorted({r["sched"] for r in rows})
    sched = scheds[0]
    rows = [r for r in rows if r["sched"] == sched]
    bss = sorted({r["bs"] for r in rows})
    modes = ["posix", "iou", "iou_active", "iou_poll"]
    data = defaultdict(dict)
    for r in rows:
        data[r["mode"]][r["bs"]] = r["median_ns"] / 1000.0  # us

    x = np.arange(len(bss))
    w = 0.2
    fig, ax = plt.subplots(figsize=(7, 4))
    for i, mode in enumerate(modes):
        ys = [data.get(mode, {}).get(bs, 0) for bs in bss]
        ax.bar(x + (i - 1.5) * w, ys, w, label=mode)
    ax.set_xticks(x)
    ax.set_xticklabels([f"{b}B" for b in bss])
    ax.set_ylabel("median latency (µs)")
    ax.set_xlabel("request size")
    ax.set_title(f"T1: single-task read latency ({sched} scheduler)")
    ax.legend()
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t1_median_latency.png")
    fig.savefig(out, dpi=130)
    print("wrote", out)


def fig_gap(rows):
    # The Finding #1 headline at 4096B: median latency of each path, annotating
    # the scheduler overhead that active-checking removes:  iou - iou_active.
    sub = {r["mode"]: r["median_ns"] / 1000.0 for r in rows if r["bs"] == 4096}
    order = [m for m in ["posix", "iou", "iou_active", "iou_poll"] if m in sub]
    if "iou" not in sub or "iou_active" not in sub:
        print("skip gap fig (need iou + iou_active at 4096B)")
        return
    sched_overhead = sub["iou"] - sub["iou_active"]
    fig, ax = plt.subplots(figsize=(6, 4))
    colors = {"posix": "#bbb", "iou": "#e76f51", "iou_active": "#2a9d8f", "iou_poll": "#264653"}
    ax.bar(order, [sub[m] for m in order], color=[colors[m] for m in order])
    ax.set_ylabel("median latency @4KB (µs)")
    ax.set_title("T1 / Finding #1: most of the interrupt path's cost is scheduling\n"
                 f"active-checking removes ≈{sched_overhead:.1f} µs (iou → iou_active)")
    ax.annotate("", xy=(1, sub["iou_active"]), xytext=(1, sub["iou"]),
                arrowprops=dict(arrowstyle="<->", color="black"))
    ax.text(1.15, (sub["iou"] + sub["iou_active"]) / 2,
            f"{sched_overhead:.1f} µs\nscheduler\noverhead", va="center", fontsize=9)
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t1_interrupt_gap.png")
    fig.savefig(out, dpi=130)
    print("wrote", out)


def main():
    if not os.path.exists(CSV):
        sys.exit(f"no data at {CSV}; run scripts/run_t1.sh first")
    rows = load(CSV)
    fig_median(rows)
    fig_gap(rows)


if __name__ == "__main__":
    main()
