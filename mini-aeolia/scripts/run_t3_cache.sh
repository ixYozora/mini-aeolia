#!/usr/bin/env bash
# T3b — effect of the mfs block cache: uncached and cached mfs against ext4 and
# f2fs on the same null_blk device.
#
# mkfs destroys the contents of $DEV. Pass the null_blk device.
#   sudo scripts/run_t3_cache.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib.sh
DEV="${1:-/dev/nullb0}"
N="${NFILES:-5000}"; F="${FSIZE:-4096}"
MNT="/mnt/mfs_ext4"
OUT="results/t3b_cache.csv"
[[ -x bin/fs_micro ]] || { echo "build first: make"; exit 1; }
umount "$MNT" 2>/dev/null || true      # a previous aborted run may have left it
require_scratch_dev "$DEV"

echo "target,op,nfiles,us_per_op,ops_per_s" > "$OUT"
echo "[*] mfs (no cache)"
MFS_CACHE_BLOCKS=0    ./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$N" --fsize "$F" --csv mfs_nocache >> "$OUT"
echo "[*] mfs (cache 8192 blocks = 32 MB)"
MFS_CACHE_BLOCKS=8192 ./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$N" --fsize "$F" --csv mfs_cache   >> "$OUT"
run_kfs(){  # $1 = fs label, $2 = mkfs command
  echo "[*] $1"
  umount "$MNT" 2>/dev/null || true
  $2 "$DEV"
  mkdir -p "$MNT"; mount "$DEV" "$MNT"; rm -rf "${MNT:?}/x"; mkdir -p "$MNT/x"
  ./bin/fs_micro --target posix --dir "$MNT/x" --nfiles "$N" --fsize "$F" --csv "$1" >> "$OUT"
  sync; umount "$MNT"
}
run_kfs ext4 "mkfs.ext4 -F -q"
run_kfs f2fs "mkfs.f2fs -f -q"
echo "[*] wrote $OUT"; column -t -s, "$OUT"
