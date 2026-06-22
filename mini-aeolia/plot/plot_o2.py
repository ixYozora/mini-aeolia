#!/usr/bin/env python3
"""O2 — device-latency sweep: absolute vs relative scheduler overhead."""
import os, csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(HERE, "results", "o2_devlat_sweep.csv")

def main():
    if not os.path.exists(CSV): sys.exit("run scripts/run_o2.sh")
    rows = sorted(csv.DictReader(open(CSV)), key=lambda r: int(r["dev_lat_ns"]))
    xs = [int(r["dev_lat_ns"]) / 1000 for r in rows]          # us
    absus = [int(r["overhead_ns"]) / 1000 for r in rows]      # us
    pct = [float(r["overhead_pct"]) for r in rows]

    fig, ax1 = plt.subplots(figsize=(7, 4.5))
    l1, = ax1.plot(xs, absus, "o-", color="#264653", label="overhead removed (µs, absolute)")
    ax1.set_xlabel("device completion latency (µs)")
    ax1.set_ylabel("scheduler overhead removed (µs)", color="#264653")
    ax1.set_xscale("log")
    ax2 = ax1.twinx()
    l2, = ax2.plot(xs, pct, "s-", color="#e76f51", label="overhead as % of total")
    ax2.set_ylabel("overhead as % of iou latency", color="#e76f51")
    ax1.set_title("O2: scheduler overhead is ~constant in µs, so its\n"
                  "relative cost shrinks as the device gets slower")
    ax1.legend(loc="upper right", fontsize=8); ax2.legend(loc="center right", fontsize=8)
    fig.tight_layout()
    out = os.path.join(HERE, "results", "o2_devlat_sweep.png"); fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__": main()
