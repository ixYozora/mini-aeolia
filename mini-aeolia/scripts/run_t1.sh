#!/usr/bin/env bash
# run_t1.sh — Test T1: single-task read-latency decomposition.
# Sweeps {posix, iou, iou_poll} x {block sizes} against the null_blk device and
# writes a CSV consumed by plot/plot_t1.py.
#
# Run as root (raw device access + stable results):
#   sudo scripts/run_t1.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."

DEV="${1:-/dev/nullb0}"
ITERS="${ITERS:-200000}"
CPU="${CPU:-2}"
OUT="results/t1_latency.csv"
PROBE="bin/lat_probe"

[[ -x "$PROBE" ]] || { echo "build first: make"; exit 1; }
[[ -b "$DEV" ]]   || { echo "block device $DEV not found; run scripts/setup_nullblk.sh"; exit 1; }

LABEL="${LABEL:-default}"   # set LABEL=active_check when running under the M2 scheduler
echo "mode,bs,iters,min_ns,median_ns,mean_ns,p99_ns,p999_ns,iops,cpu_util,sched" > "$OUT"

for bs in 4096 16384 131072; do
  for mode in posix iou iou_active iou_poll; do
    echo "[*] $mode bs=$bs sched=$LABEL"
    line=$("$PROBE" --dev "$DEV" --mode "$mode" --bs "$bs" --iters "$ITERS" --cpu "$CPU" --csv) \
      || { echo "  (skipped: $mode bs=$bs failed — e.g. iou_poll needs poll_queues)"; continue; }
    echo "${line},${LABEL}" >> "$OUT"
  done
done

echo "[*] wrote $OUT"
column -t -s, "$OUT"
