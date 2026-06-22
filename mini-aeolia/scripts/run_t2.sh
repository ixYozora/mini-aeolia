#!/usr/bin/env bash
# run_t2.sh — Test T2: coordinated scheduling tail latency.
# Compares LC-thread tail latency under three schedulers:
#   default   kernel EEVDF (no sched_ext)
#   scx_fifo  miniaeo with coordinate=0 (custom scheduler, no LC awareness)
#   scx_coord miniaeo with coordinate=1 (LC tasks preempt compute hogs)
#
#   sudo scripts/run_t2.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."

DEV="${1:-/dev/nullb0}"
HOGS="${HOGS:-3}"
CPU="${CPU:-3}"
ITERS="${ITERS:-100000}"
OUT="results/t2_coexist.csv"
LOADER="sched/miniaeo_loader"

[[ -x bin/coexist ]] || { echo "build first: make"; exit 1; }
[[ -x "$LOADER" ]]   || { echo "build scheduler: make -C sched"; exit 1; }
[[ -b "$DEV" ]]      || { echo "$DEV missing; run scripts/setup_nullblk.sh"; exit 1; }

echo "config,hogs,cpu,median_ns,p99_ns,p999_ns,p9999_ns" > "$OUT"

run_coexist() { # $1 = label
  ./bin/coexist --dev "$DEV" --hogs "$HOGS" --cpu "$CPU" --iters "$ITERS" --csv "$1" >> "$OUT"
}

start_sched() { # $1 = coordinate flag
  "$LOADER" "$1" >/tmp/miniaeo_ready 2>/tmp/miniaeo_log &
  LPID=$!
  # wait for READY (struct_ops attached)
  for _ in $(seq 1 50); do grep -q READY /tmp/miniaeo_ready 2>/dev/null && return 0; sleep 0.1; done
  echo "scheduler failed to attach:"; cat /tmp/miniaeo_log; kill $LPID 2>/dev/null || true; exit 1
}
stop_sched() { kill -TERM "$LPID" 2>/dev/null || true; wait "$LPID" 2>/dev/null || true; sleep 0.3; }

echo "[*] default (kernel EEVDF)";        run_coexist default
echo "[*] scx_fifo (coordinate=0)";       start_sched 0; run_coexist scx_fifo;  stop_sched
echo "[*] scx_coord (coordinate=1)";      start_sched 1; run_coexist scx_coord; stop_sched

echo "[*] wrote $OUT"
column -t -s, "$OUT"
