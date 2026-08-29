#!/usr/bin/env bash
# run_all.sh — build, set up the device, run the reproduction suite and
# generate the figures.  Run as root:  sudo scripts/run_all.sh
#
# The scripts/run_o*.sh experiments are left out. run_o2.sh recreates the
# device at different latencies and would disturb the other runs, so run those
# separately once this has finished.
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
echo "== T1b queue-depth sweep =="
scripts/run_t1_depth.sh "$DEV"
echo "== T2 coordinated scheduling =="
ITERS="${T2_ITERS:-30000}" scripts/run_t2.sh "$DEV"
echo "== T2c latency vs throughput across policies =="
scripts/run_sched.sh "$DEV"
echo "== T3 mfs vs ext4 =="
scripts/run_t3.sh "$DEV"
echo "== T3b mfs buffer cache vs ext4/f2fs =="
scripts/run_t3_cache.sh "$DEV"
echo "== T4 SPDK kernel bypass (optional, needs SPDK built) =="
if [[ -x "${SPDK_DIR:-$PWD/../spdk}/build/examples/bdevperf" ]]; then
  scripts/run_m4_spdk.sh
else
  echo "  (SPDK not built; skipping T4. Set SPDK_DIR to enable it.)"
fi
echo "== T5 fio baselines =="
scripts/run_baselines.sh "$DEV"

echo "== figures =="
for p in t1 t1_depth t2 t3 t3b sched t4; do python3 "plot/plot_${p}.py" || true; done

echo "== done. results in results/ =="
ls -1 results/*.png results/*.csv 2>/dev/null
