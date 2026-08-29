#!/usr/bin/env bash
# run_baselines.sh — Test T5: the baseline storage columns of Aeolia's
# evaluation, measured with fio. The Aeolia and AeoFS columns need UINTR and
# MPK and are not reproducible here. The baselines are:
#   psync            -> POSIX
#   io_uring         -> io_uring default (interrupt)
#   io_uring+hipri   -> io_uring polling
# 4 KB random read, depth 1, single job, on the null_blk device.
#
#   sudo scripts/run_baselines.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."

DEV="${1:-/dev/nullb0}"
RUNTIME="${RUNTIME:-15}"
OUT="results/t5_fio_baselines.csv"
command -v fio >/dev/null || { echo "fio not installed"; exit 1; }
[[ -b "$DEV" ]] || { echo "$DEV missing"; exit 1; }

echo "baseline,iops,lat_median_ns,lat_p99_ns" > "$OUT"

run() { # $1 label  $2 engine  $3 extra
  echo "[*] $1"
  json=$(fio --name="$1" --filename="$DEV" --direct=1 --rw=randread --bs=4k \
    --iodepth=1 --numjobs=1 --ioengine="$2" $3 --runtime="$RUNTIME" --time_based \
    --output-format=json 2>/dev/null)
  iops=$(echo "$json"   | jq '.jobs[0].read.iops')
  med=$(echo "$json"    | jq '.jobs[0].read.clat_ns.percentile."50.000000" // .jobs[0].read.clat_ns.mean')
  p99=$(echo "$json"    | jq '.jobs[0].read.clat_ns.percentile."99.000000"')
  echo "$1,$iops,$med,$p99" >> "$OUT"
}

run "psync(POSIX)"      psync     ""
run "io_uring(intr)"    io_uring  ""
run "io_uring(poll)"    io_uring  "--hipri=1"

echo "[*] wrote $OUT"
column -t -s, "$OUT"
