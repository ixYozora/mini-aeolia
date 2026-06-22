#!/usr/bin/env bash
# run_t3.sh — Test T3: mini-libFS vs ext4 micro-benchmarks on the SAME device.
# mfs runs directly on the raw null_blk device; ext4 is freshly mkfs'd on the
# same device and mounted. Apples-to-apples (same storage, same workload).
#
#   sudo scripts/run_t3.sh [/dev/nullb0]
set -euo pipefail
cd "$(dirname "$0")/.."

DEV="${1:-/dev/nullb0}"
NFILES="${NFILES:-5000}"
FSIZE="${FSIZE:-4096}"
MNT="/mnt/mfs_ext4"
OUT="results/t3_fs_micro.csv"

[[ -x bin/fs_micro ]] || { echo "build first: make"; exit 1; }
[[ -b "$DEV" ]] || { echo "$DEV missing; run scripts/setup_nullblk.sh"; exit 1; }

echo "target,op,nfiles,us_per_op,ops_per_s" > "$OUT"

echo "[*] mfs on $DEV"
./bin/fs_micro --target mfs --dev "$DEV" --nfiles "$NFILES" --fsize "$FSIZE" --csv mfs >> "$OUT"

echo "[*] ext4 on $DEV (mkfs + mount)"
umount "$MNT" 2>/dev/null || true
mkfs.ext4 -F -q "$DEV"
mkdir -p "$MNT"
mount "$DEV" "$MNT"
rm -rf "${MNT:?}/x"; mkdir -p "$MNT/x"
./bin/fs_micro --target posix --dir "$MNT/x" --nfiles "$NFILES" --fsize "$FSIZE" --csv ext4 >> "$OUT"
sync; umount "$MNT"

echo "[*] wrote $OUT"
column -t -s, "$OUT"
