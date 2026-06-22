#!/usr/bin/env bash
# run_all.sh — one-shot: build, set up device, run every test, generate figures.
# Run as root (or with passwordless sudo):  sudo scripts/run_all.sh
set -euo pipefail
cd "$(dirname "$0")/.."

DEV="${DEV:-/dev/nullb0}"

echo "== build =="
make
make -C sched

echo "== device =="
[[ -b "$DEV" ]] || scripts/setup_nullblk.sh

echo "== T1 latency decomposition =="
scripts/run_t1.sh "$DEV"
echo "== T2 coordinated scheduling =="
ITERS="${T2_ITERS:-30000}" scripts/run_t2.sh "$DEV"
echo "== T3 mini-libFS vs ext4 =="
scripts/run_t3.sh "$DEV"
echo "== T5 fio baselines =="
scripts/run_baselines.sh "$DEV"

echo "== figures =="
for p in t1 t2 t3; do python3 "plot/plot_${p}.py" || true; done

echo "== done. results in results/ =="
ls -1 results/*.png results/*.csv 2>/dev/null
