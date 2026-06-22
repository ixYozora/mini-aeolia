#!/usr/bin/env python3
"""T4 — backend comparison: io_uring paths vs SPDK kernel-bypass, depth-1 4 KB.

Combines T1 (io_uring on null_blk @3 µs) with M4 (SPDK polling on a RAM+delay
bdev) to contrast the two userspace storage stacks at the same device latency.
"""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T1 = os.path.join(HERE, "results", "t1_latency.csv")
T4 = os.path.join(HERE, "results", "t4_spdk.csv")

def main():
    if not (os.path.exists(T1) and os.path.exists(T4)):
        sys.exit("need results/t1_latency.csv and results/t4_spdk.csv")
    # io_uring medians at 4 KB (default scheduler)
    iou = {}
    for r in csv.DictReader(open(T1)):
        if int(r["bs"]) == 4096 and r.get("sched", "default") == "default":
            iou[r["mode"]] = float(r["median_ns"]) / 1000.0
    spdk = {r["backend"]: float(r["median_us"]) for r in csv.DictReader(open(T4))}

    bars = [
        ("posix",         iou.get("posix"),      "#bbb",     "io_uring/kernel"),
        ("iou",           iou.get("iou"),        "#e76f51",  "io_uring/kernel"),
        ("iou_active",    iou.get("iou_active"), "#f4a261",  "io_uring/kernel"),
        ("iou_poll",      iou.get("iou_poll"),   "#e9c46a",  "io_uring/kernel"),
        ("spdk_delay\n(3µs, bypass)", spdk.get("spdk_delay"), "#2a9d8f", "SPDK/bypass"),
        ("spdk_malloc\n(0µs floor)",  spdk.get("spdk_malloc"),"#264653", "SPDK/bypass"),
    ]
    bars = [b for b in bars if b[1] is not None]
    labels = [b[0] for b in bars]; vals = [b[1] for b in bars]; cols = [b[2] for b in bars]

    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.bar(labels, vals, color=cols)
    for i, v in enumerate(vals):
        ax.text(i, v, f"{v:.2f}", ha="center", va="bottom", fontsize=8)
    ax.set_ylabel("median latency @4 KB depth-1 (µs)")
    ax.set_title("T4: io_uring vs SPDK kernel-bypass at a 3 µs device\n"
                 "full kernel bypass (SPDK 3.9 µs) beats io_uring interrupt (7.4) and active-checking (5.3)")
    fig.tight_layout()
    out = os.path.join(HERE, "results", "t4_backend.png"); fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__": main()
