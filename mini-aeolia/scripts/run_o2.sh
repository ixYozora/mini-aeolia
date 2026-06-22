#!/usr/bin/env bash
# O2 — device-latency sweep: how the scheduler overhead's significance changes
# with device latency (the experiment the paper's fixed Optane could not do).
#   sudo scripts/run_o2.sh
set -euo pipefail
cd "$(dirname "$0")/.."
DEV=/dev/nullb0
ITERS="${ITERS:-100000}"
CPU="${CPU:-2}"
OUT="results/o2_devlat_sweep.csv"
P="bin/lat_probe"
[[ -x "$P" ]] || { echo "build first: make"; exit 1; }

echo "dev_lat_ns,iou_median_ns,active_median_ns,overhead_ns,overhead_pct" > "$OUT"
for comp in 1000 3000 6000 12000 25000 50000; do
  echo "[*] recreating null_blk completion=${comp}ns"
  IRQMODE=2 COMPLETION_NSEC="$comp" POLL_QUEUES=4 bash scripts/setup_nullblk.sh >/dev/null
  iou=$("$P"    --dev "$DEV" --mode iou        --bs 4096 --iters "$ITERS" --cpu "$CPU" --csv | cut -d, -f5)
  act=$("$P"    --dev "$DEV" --mode iou_active --bs 4096 --iters "$ITERS" --cpu "$CPU" --spin 100 --csv | cut -d, -f5)
  ovh=$(( iou - act ))
  pct=$(awk "BEGIN{printf \"%.1f\", ($iou>0)?100.0*$ovh/$iou:0}")
  echo "$comp,$iou,$act,$ovh,$pct" >> "$OUT"
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
