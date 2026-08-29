#!/usr/bin/env bash
# T2c — scheduling-policy comparison across contention: default (kernel EEVDF)
# vs preempt (blunt LC priority) vs fair (weighted virtual time). Captures both
# LC tail latency and compute throughput, which is what makes the trade-off
# between the two visible.
#   sudo scripts/run_sched.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib.sh

DEV="${1:-/dev/nullb0}"; CPU="${CPU:-3}"; ITERS="${ITERS:-15000}"
OUT="results/sched_policies.csv"
[[ -x bin/coexist && -x "$LOADER" ]] || { echo "build: make && make -C sched"; exit 1; }
[[ -b "$DEV" ]] || { echo "$DEV missing; run scripts/setup_nullblk.sh"; exit 1; }

echo "config,hogs,p999_ns,hog_batches_s" > "$OUT"
# coexist emits config,hogs,cpu,median,p99,p999,p9999,hog_batches_s
run(){ ./bin/coexist --dev "$DEV" --hogs "$1" --cpu "$CPU" --iters "$ITERS" --csv "$2" \
       | awk -F, -v c="$2" -v h="$1" '{print c","h","$6","$8}' >> "$OUT"; }

for h in 1 2 4 8; do
  echo "[*] hogs=$h default"; run "$h" default
  echo "[*] hogs=$h preempt"; sched_start 1; run "$h" preempt; sched_check; sched_stop
  echo "[*] hogs=$h fair";    sched_start 2; run "$h" fair;    sched_check; sched_stop
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
