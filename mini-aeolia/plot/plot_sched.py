#!/usr/bin/env python3
"""T2c/O4b — three scheduling policies: the latency/throughput trade-off.

Two figures vs number of compute hogs:
  sched_latency.png     LC p99.9 latency  (lower = better)
  sched_throughput.png  compute throughput (higher = better)
default / preempt / fair. The point: preempt buys latency at a throughput cost,
fair does the opposite, neither gets both -> why Aeolia needs UINTR.
"""
import os, csv, sys
from collections import defaultdict
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "sched_policies.csv")
STYLE = {"default": ("#264653", "o-"), "preempt": ("#e76f51", "s-"), "fair": ("#2a9d8f", "^-")}

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_sched.sh")
    lat = defaultdict(dict); thr = defaultdict(dict)
    for r in csv.DictReader(open(CSV)):
        h = int(r["hogs"])
        lat[r["config"]][h] = float(r["p999_ns"]) / 1000.0      # us
        thr[r["config"]][h] = float(r["hog_batches_s"])
    configs = [c for c in ("default", "preempt", "fair") if c in lat]
    hs = sorted(next(iter(lat.values())).keys())

    fig, ax = plt.subplots(figsize=(7, 4))
    for c in configs:
        col, mk = STYLE[c]
        ax.plot(hs, [lat[c][h] for h in hs], mk, color=col, label=c)
    ax.set_yscale("log"); ax.set_xlabel("compute hogs on the core")
    ax.set_ylabel("LC p99.9 latency (µs)  ↓ better")
    ax.set_title("Scheduling policies: LC tail latency")
    ax.legend(); fig.tight_layout()
    o = os.path.join(HERE, "results", "sched_latency.png"); fig.savefig(o, dpi=130); print("wrote", o)

    fig, ax = plt.subplots(figsize=(7, 4))
    for c in configs:
        col, mk = STYLE[c]
        ax.plot(hs, [thr[c][h] for h in hs], mk, color=col, label=c)
    ax.set_xlabel("compute hogs on the core")
    ax.set_ylabel("compute throughput (batches/s)  ↑ better")
    ax.set_title("Scheduling policies: compute throughput")
    ax.legend(); fig.tight_layout()
    o = os.path.join(HERE, "results", "sched_throughput.png"); fig.savefig(o, dpi=130); print("wrote", o)

if __name__ == "__main__":
    main()
