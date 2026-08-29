#!/usr/bin/env bash
# run_t3.sh — Test T3: mfs vs ext4 micro-benchmarks on the same device.
# mfs runs directly on the raw null_blk device; ext4 is freshly mkfs'd on the
# same device and mounted, so both see identical storage and workload.
#
# mkfs destroys the contents of $DEV. Pass the null_blk device.
#
#   sudo scripts/run_t3.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib.sh

DEV="${1:-/dev/nullb0}"
NFILES="${NFILES:-5000}"
FSIZE="${FSIZE:-4096}"
MNT="/mnt/mfs_ext4"
OUT="results/t3_fs_micro.csv"

[[ -x bin/fs_micro ]] || { echo "build first: make"; exit 1; }
umount "$MNT" 2>/dev/null || true      # a previous aborted run may have left it
require_scratch_dev "$DEV"

echo "target,op,nfiles,us_per_op,ops_per_s" > "$OUT"

echo "[*] mfs on $DEV"
./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$NFILES" --fsize "$FSIZE" --csv mfs >> "$OUT"

echo "[*] ext4 on $DEV (mkfs + mount)"
mkfs.ext4 -F -q "$DEV"
mkdir -p "$MNT"
mount "$DEV" "$MNT"
rm -rf "${MNT:?}/x"; mkdir -p "$MNT/x"
./bin/fs_micro --target posix --dir "$MNT/x" --nfiles "$NFILES" --fsize "$FSIZE" --csv ext4 >> "$OUT"
sync; umount "$MNT"

echo "[*] wrote $OUT"
column -t -s, "$OUT"
