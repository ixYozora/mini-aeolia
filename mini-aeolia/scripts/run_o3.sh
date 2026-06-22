#!/usr/bin/env bash
# O3 + O4 — contention sweep: LC tail latency AND compute-hog throughput under
# default vs the coordinated scheduler, as the number of hogs grows.
#   sudo scripts/run_o3.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"
CPU="${CPU:-3}"
ITERS="${ITERS:-20000}"
OUT="results/o3_contention.csv"
LOADER="sched/miniaeo_loader"
[[ -x bin/coexist && -x "$LOADER" ]] || { echo "build: make && make -C sched"; exit 1; }

echo "config,hogs,p999_ns,hog_batches_s" > "$OUT"

run() { ./bin/coexist --dev "$DEV" --hogs "$1" --cpu "$CPU" --iters "$ITERS" --csv "$2" \
        | awk -F, -v c="$2" -v h="$1" '{print c","h","$6","$8}' >> "$OUT"; }

start(){ "$LOADER" 1 >/tmp/o3_ready 2>/tmp/o3_log & LPID=$!
  for _ in $(seq 1 50); do grep -q READY /tmp/o3_ready 2>/dev/null && return 0; sleep 0.1; done
  echo "sched attach failed"; cat /tmp/o3_log; exit 1; }
stop(){ kill -TERM "$LPID" 2>/dev/null || true; wait "$LPID" 2>/dev/null || true; sleep 0.3; }

for h in 0 1 2 4 6 8; do
  echo "[*] hogs=$h default";   run "$h" default
  echo "[*] hogs=$h scx_coord"; start; run "$h" scx_coord; stop
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
