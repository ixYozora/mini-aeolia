#!/usr/bin/env bash
# T1b — queue-depth sweep (extends T1 beyond depth-1): io_uring interrupt vs
# polling, IOPS + mean latency as queue depth grows. Shows how the poll/interrupt
# gap behaves once requests are batched (depth > 1). Uses fio on null_blk.
#   sudo scripts/run_t1_depth.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"; RT="${RUNTIME:-5}"
OUT="results/t1_depth.csv"
command -v fio >/dev/null || { echo "fio not installed"; exit 1; }

echo "engine,qd,iops,lat_mean_ns" > "$OUT"
run(){  # $1 label  $2 ioengine  $3 extra
  for qd in 1 2 4 8 16 32 64; do
    j=$(fio --name=d --filename="$DEV" --direct=1 --rw=randread --bs=4k --iodepth="$qd" \
        --numjobs=1 --ioengine="$2" $3 --runtime="$RT" --time_based --output-format=json 2>/dev/null)
    iops=$(echo "$j" | jq '.jobs[0].read.iops')
    lat=$(echo "$j"  | jq '.jobs[0].read.clat_ns.mean')
    echo "$1,$qd,$iops,$lat" >> "$OUT"
    echo "  $1 qd=$qd -> $(printf '%.0f' "$iops") IOPS"
  done
}
run interrupt io_uring ""
run poll      io_uring "--hipri=1"
echo "[*] wrote $OUT"; column -t -s, "$OUT"
