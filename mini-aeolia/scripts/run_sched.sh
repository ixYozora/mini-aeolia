#!/usr/bin/env bash
# T2c / O4b — scheduling-policy comparison across contention:
# default (kernel EEVDF) vs preempt (blunt LC priority) vs fair (vtime/EEVDF-like).
# Captures BOTH LC tail latency and compute throughput -> the trade-off.
#   sudo scripts/run_sched.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"; CPU="${CPU:-3}"; ITERS="${ITERS:-15000}"
OUT="results/sched_policies.csv"
LOADER="sched/miniaeo_loader"
[[ -x bin/coexist && -x "$LOADER" ]] || { echo "build: make && make -C sched"; exit 1; }

echo "config,hogs,p999_ns,hog_batches_s" > "$OUT"
run(){ ./bin/coexist --dev "$DEV" --hogs "$1" --cpu "$CPU" --iters "$ITERS" --csv "$2" \
       | awk -F, -v c="$2" -v h="$1" '{print c","h","$6","$8}' >> "$OUT"; }
start(){ "$LOADER" "$1" >/tmp/sld 2>/dev/null & LP=$!;
         for _ in $(seq 50); do grep -q READY /tmp/sld 2>/dev/null && return; sleep 0.1; done;
         echo "sched attach failed"; exit 1; }
stop(){ kill -TERM "$LP" 2>/dev/null || true; wait "$LP" 2>/dev/null || true; sleep 0.3; }

for h in 1 2 4 8; do
  echo "[*] hogs=$h default"; run "$h" default
  echo "[*] hogs=$h preempt"; start 1; run "$h" preempt; stop
  echo "[*] hogs=$h fair";    start 2; run "$h" fair;    stop
done
echo "[*] wrote $OUT"; column -t -s, "$OUT"
