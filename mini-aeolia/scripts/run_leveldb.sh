#!/usr/bin/env bash
# T6 — LevelDB db_bench on ext4 vs f2fs (the paper's Table 7/8 BASELINE columns;
# the AeoFS column needs UINTR/MPK and is not reproducible). Application-level.
#   sudo scripts/run_leveldb.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"
DB_BENCH="${DB_BENCH:-/home/yozora/Desktop/Software-Seminar/Aeolia/benchmarks/leveldb/build/db_bench}"
MNT="/mnt/mfs_ext4"; N="${NUM:-100000}"
BENCH="fillseq,fillrandom,fillsync,readrandom,deleterandom"
OUT="results/t6_leveldb.csv"
[[ -x "$DB_BENCH" ]] || { echo "build db_bench first (see docs/15)"; exit 1; }

echo "fs,bench,ops_per_s" > "$OUT"
run_fs(){  # $1 = fs label, $2 = mkfs command
  echo "[*] $1"
  umount "$MNT" 2>/dev/null || true
  $2 "$DEV"; mkdir -p "$MNT"; mount "$DEV" "$MNT"; rm -rf "${MNT:?}/ldb"; mkdir -p "$MNT/ldb"
  "$DB_BENCH" --db="$MNT/ldb" --benchmarks="$BENCH" --num="$N" 2>&1 \
    | tr '\r' '\n' \
    | awk -v fs="$1" '/ ops\/s;/ { printf "%s,%s,%.0f\n", fs, $1, $3 }' >> "$OUT"
  sync; umount "$MNT"
}
run_fs ext4 "mkfs.ext4 -F -q"
run_fs f2fs "mkfs.f2fs -f -q"
echo "[*] wrote $OUT"; column -t -s, "$OUT"
