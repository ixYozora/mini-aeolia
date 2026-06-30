#!/usr/bin/env bash
# T3b — mfs buffer-cache comparison: nocache vs cache vs ext4 (same null_blk).
#   sudo scripts/run_t3_cache.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
DEV="${1:-/dev/nullb0}"
N="${NFILES:-5000}"; F="${FSIZE:-4096}"
MNT="/mnt/mfs_ext4"
OUT="results/t3b_cache.csv"
[[ -x bin/fs_micro ]] || { echo "build first: make"; exit 1; }
[[ -b "$DEV" ]] || { echo "$DEV missing"; exit 1; }

echo "target,op,nfiles,us_per_op,ops_per_s" > "$OUT"
echo "[*] mfs (no cache)"
MFS_CACHE_BLOCKS=0    ./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$N" --fsize "$F" --csv mfs_nocache >> "$OUT"
echo "[*] mfs (cache 8192 blocks = 32 MB)"
MFS_CACHE_BLOCKS=8192 ./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$N" --fsize "$F" --csv mfs_cache   >> "$OUT"
echo "[*] ext4"
umount "$MNT" 2>/dev/null || true
mkfs.ext4 -F -q "$DEV"; mkdir -p "$MNT"; mount "$DEV" "$MNT"; rm -rf "${MNT:?}/x"; mkdir -p "$MNT/x"
./bin/fs_micro --target posix --dir "$MNT/x" --nfiles "$N" --fsize "$F" --csv ext4 >> "$OUT"
sync; umount "$MNT"
echo "[*] wrote $OUT"; column -t -s, "$OUT"
