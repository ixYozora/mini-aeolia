#!/usr/bin/env python3
"""T1b — IOPS vs queue depth: io_uring interrupt vs polling."""
import os, csv, sys
from collections import defaultdict
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "t1_depth.csv")

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_t1_depth.sh")
    iops = defaultdict(dict)
    for r in csv.DictReader(open(CSV)):
        iops[r["engine"]][int(r["qd"])] = float(r["iops"])
    fig, ax = plt.subplots(figsize=(7, 4))
    sty = {"interrupt": ("#e76f51", "o-"), "poll": ("#2a9d8f", "s-")}
    for eng in ("interrupt", "poll"):
        if eng not in iops: continue
        qds = sorted(iops[eng]); col, mk = sty[eng]
        ax.plot(qds, [iops[eng][q]/1e6 for q in qds], mk, color=col, label=eng)
    ax.set_xscale("log", base=2); ax.set_xlabel("queue depth")
    ax.set_ylabel("IOPS (millions)")
    ax.set_title("T1b: throughput vs queue depth (io_uring)\n"
                 "poll leads at low depth; both saturate as depth grows")
    ax.legend(); fig.tight_layout()
    out = os.path.join(HERE, "results", "t1_depth.png"); fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__":
    main()
