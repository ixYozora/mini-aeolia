#!/usr/bin/env python3
"""O3 + O4 — contention sweep: tail latency and compute-throughput cost."""
import os, csv, sys
from collections import defaultdict
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "o3_contention.csv")

def load():
    p999 = defaultdict(dict); hog = defaultdict(dict)
    for r in csv.DictReader(open(CSV)):
        h = int(r["hogs"])
        p999[r["config"]][h] = float(r["p999_ns"]) / 1000.0     # us
        hog[r["config"]][h] = float(r["hog_batches_s"])
    return p999, hog

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_o3.sh")
    p999, hog = load()
    configs = [c for c in ("default", "scx_coord") if c in p999]
    hs = sorted(next(iter(p999.values())).keys())

    # O3 — tail latency vs contention
    fig, ax = plt.subplots(figsize=(7, 4))
    for c in configs:
        ax.plot(hs, [p999[c][h] for h in hs], "o-", label=c)
    ax.set_yscale("log"); ax.set_xlabel("number of compute hogs on the core")
    ax.set_ylabel("LC p99.9 latency (µs)")
    ax.set_title("O3: coordination benefit grows with contention")
    ax.legend(); fig.tight_layout()
    o = os.path.join(HERE, "results", "o3_contention.png"); fig.savefig(o, dpi=130); print("wrote", o)

    # O4 — compute throughput retained under coordination
    fig, ax = plt.subplots(figsize=(7, 4))
    for c in configs:
        ax.plot(hs, [hog[c][h] for h in hs], "s-", label=c)
    ax.set_xlabel("number of compute hogs on the core")
    ax.set_ylabel("compute-hog throughput (batches/s)")
    ax.set_title("O4: throughput cost of protecting LC latency\n(gap between lines = compute sacrificed)")
    ax.legend(); fig.tight_layout()
    o = os.path.join(HERE, "results", "o4_throughput_cost.png"); fig.savefig(o, dpi=130); print("wrote", o)

    # headline ratio at max contention
    if "default" in hog and "scx_coord" in hog and hs:
        h = hs[-1]; d = hog["default"][h]; c = hog["scx_coord"][h]
        if d: print(f"at {h} hogs: scx_coord retains {100*c/d:.0f}% of compute throughput vs default")

if __name__ == "__main__": main()
