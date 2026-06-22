#!/usr/bin/env bash
# O1 — active-checking spin-budget sweep: latency vs CPU utilization.
#   sudo scripts/run_o1.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"
ITERS="${ITERS:-100000}"
CPU="${CPU:-2}"
OUT="results/o1_spin_sweep.csv"
P="bin/lat_probe"
[[ -x "$P" ]] || { echo "build first: make"; exit 1; }

echo "mode,spin_us,median_ns,p99_ns,cpu_util" > "$OUT"
# reference: blocking interrupt path and polling
for ref in iou iou_poll; do
  l=$("$P" --dev "$DEV" --mode "$ref" --bs 4096 --iters "$ITERS" --cpu "$CPU" --csv) || continue
  echo "$ref,NA,$(echo "$l"|cut -d, -f5),$(echo "$l"|cut -d, -f7),$(echo "$l"|cut -d, -f10)" >> "$OUT"
done
# active-checking with increasing spin budget
for s in 0 1 2 3 5 8 12 20 50 100 200; do
  echo "[*] iou_active spin=${s}us"
  l=$("$P" --dev "$DEV" --mode iou_active --bs 4096 --iters "$ITERS" --cpu "$CPU" --spin "$s" --csv)
  echo "iou_active,$s,$(echo "$l"|cut -d, -f5),$(echo "$l"|cut -d, -f7),$(echo "$l"|cut -d, -f10)" >> "$OUT"
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
