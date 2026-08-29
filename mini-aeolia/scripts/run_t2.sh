#!/usr/bin/env bash
# run_t2.sh — Test T2: coordinated scheduling tail latency.
# Compares LC-thread tail latency under three schedulers:
#   default    kernel EEVDF, no sched_ext
#   scx_fifo   miniaeo mode 0, a custom scheduler without LC awareness
#   scx_coord  miniaeo mode 1, LC tasks preempt the compute hogs
#
#   sudo scripts/run_t2.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib.sh

DEV="${1:-/dev/nullb0}"
HOGS="${HOGS:-3}"
CPU="${CPU:-3}"
ITERS="${ITERS:-100000}"
OUT="results/t2_coexist.csv"

[[ -x bin/coexist ]] || { echo "build first: make"; exit 1; }
[[ -x "$LOADER" ]]   || { echo "build scheduler: make -C sched"; exit 1; }
[[ -b "$DEV" ]]      || { echo "$DEV missing; run scripts/setup_nullblk.sh"; exit 1; }

echo "config,hogs,cpu,median_ns,p99_ns,p999_ns,p9999_ns,hog_batches_s" > "$OUT"

run_coexist() { # $1 = label
  ./bin/coexist --dev "$DEV" --hogs "$HOGS" --cpu "$CPU" --iters "$ITERS" --csv "$1" >> "$OUT"
}

echo "[*] default (kernel EEVDF)"
run_coexist default
echo "[*] scx_fifo (miniaeo mode 0)"
sched_start 0; run_coexist scx_fifo;  sched_check; sched_stop
echo "[*] scx_coord (miniaeo mode 1)"
sched_start 1; run_coexist scx_coord; sched_check; sched_stop

echo "[*] wrote $OUT"
column -t -s, "$OUT"
