#!/usr/bin/env python3
"""O1 — spin-budget sweep: latency vs CPU utilization (the cost of active-checking)."""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "o1_spin_sweep.csv")

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_o1.sh")
    rows = list(csv.DictReader(open(CSV)))
    act = [r for r in rows if r["mode"] == "iou_active"]
    act.sort(key=lambda r: int(r["spin_us"]))
    xs = [int(r["spin_us"]) for r in act]
    lat = [float(r["median_ns"]) / 1000 for r in act]
    util = [float(r["cpu_util"]) * 100 for r in act]
    ref = {r["mode"]: float(r["median_ns"]) / 1000 for r in rows if r["mode"] in ("iou", "iou_poll")}

    fig, ax1 = plt.subplots(figsize=(7, 4.5))
    l1, = ax1.plot(xs, lat, "o-", color="#e76f51", label="median latency")
    ax1.set_xlabel("active-checking spin budget (µs)")
    ax1.set_ylabel("median latency (µs)", color="#e76f51")
    if "iou" in ref: ax1.axhline(ref["iou"], ls="--", color="#aaa", lw=1, label="iou (block)")
    if "iou_poll" in ref: ax1.axhline(ref["iou_poll"], ls=":", color="#888", lw=1, label="iou_poll")
    ax2 = ax1.twinx()
    l2, = ax2.plot(xs, util, "s-", color="#2a9d8f", label="CPU utilization")
    ax2.set_ylabel("CPU utilization (%)", color="#2a9d8f"); ax2.set_ylim(0, 105)
    ax1.set_title("O1: active-checking trades CPU for latency\n(latency plateaus once spin ≳ device latency; CPU → 100%)")
    lines = [l1, l2] + ax1.get_lines()[1:]
    ax1.legend(loc="center right", fontsize=8)
    fig.tight_layout()
    out = os.path.join(HERE, "results", "o1_spin_sweep.png"); fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__": main()
