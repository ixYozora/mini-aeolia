#!/usr/bin/env bash
# O3 and O4 — contention sweep: LC tail latency and compute-hog throughput under
# the default scheduler vs the coordinated one, as the number of hogs grows.
#   sudo scripts/run_o3.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib.sh

DEV="${1:-/dev/nullb0}"
CPU="${CPU:-3}"
ITERS="${ITERS:-20000}"
OUT="results/o3_contention.csv"
[[ -x bin/coexist && -x "$LOADER" ]] || { echo "build: make && make -C sched"; exit 1; }
[[ -b "$DEV" ]] || { echo "$DEV missing; run scripts/setup_nullblk.sh"; exit 1; }

echo "config,hogs,p999_ns,hog_batches_s" > "$OUT"
# coexist emits config,hogs,cpu,median,p99,p999,p9999,hog_batches_s
run() { ./bin/coexist --dev "$DEV" --hogs "$1" --cpu "$CPU" --iters "$ITERS" --csv "$2" \
        | awk -F, -v c="$2" -v h="$1" '{print c","h","$6","$8}' >> "$OUT"; }

for h in 0 1 2 4 6 8; do
  echo "[*] hogs=$h default";   run "$h" default
  echo "[*] hogs=$h scx_coord"; sched_start 1; run "$h" scx_coord; sched_check; sched_stop
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
