#!/usr/bin/env bash
# Test T4 — SPDK userspace-polling backend, a real kernel-bypass comparison
# point for the io_uring paths measured in T1.
#
# SAFETY: SPDK's own scripts/setup.sh binds *all* NVMe controllers (including the
# BOOT DISK) to VFIO and would brick the system. This script NEVER calls it.
# Instead it allocates hugepages manually and runs bdevperf with --no-pci, so
# SPDK touches no PCI device at all. Backends are pure RAM:
#   Malloc0  zero-latency RAM bdev, the floor of the SPDK stack
#   Delay0   3 µs delay on Malloc0, matching the null_blk device used in T1
#
#   sudo scripts/run_m4_spdk.sh
set -euo pipefail
cd "$(dirname "$0")/.."

#   SPDK_DIR=/path/to/spdk sudo -E scripts/run_m4_spdk.sh
SPDK_DIR="${SPDK_DIR:-$PWD/../spdk}"
BDEVPERF="$SPDK_DIR/build/examples/bdevperf"
RUNTIME="${RUNTIME:-10}"
OUT="results/t4_spdk.csv"
[[ -x "$BDEVPERF" ]] || {
    echo "bdevperf not found at $BDEVPERF"
    echo "build SPDK, then point SPDK_DIR at it:  SPDK_DIR=/path/to/spdk sudo -E $0"
    exit 1; }

echo "[*] allocating hugepages (no device binding, no spdk setup.sh)"
[[ $EUID -eq 0 ]] || { echo "must run as root (sudo)"; exit 1; }
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

cat > /tmp/ma_spdk_malloc.json <<'EOF'
{ "subsystems":[ {"subsystem":"bdev","config":[
  {"method":"bdev_malloc_create","params":{"name":"Malloc0","num_blocks":131072,"block_size":4096}}
]}]}
EOF
cat > /tmp/ma_spdk_delay.json <<'EOF'
{ "subsystems":[ {"subsystem":"bdev","config":[
  {"method":"bdev_malloc_create","params":{"name":"Malloc0","num_blocks":131072,"block_size":4096}},
  {"method":"bdev_delay_create","params":{"base_bdev_name":"Malloc0","name":"Delay0","avg_read_latency":3,"p99_read_latency":3,"avg_write_latency":3,"p99_write_latency":3}}
]}]}
EOF
printf '[global]\nfilename=Delay0\n[job0]\n' > /tmp/ma_spdk_job.ini

# bdevperf summary line: "<bdev> : runtime IOPS MiB/s Fail/s TO/s Average min max".
# Average is a mean, not a median, so the CSV column is named accordingly.
parse() { awk -v b="$1" '$1==b {print $4","$8}'; }  # -> iops,mean_us

echo "backend,dev_lat_us,mean_us,iops" > "$OUT"

echo "[*] SPDK Malloc0 (0 µs, stack floor)"
r=$("$BDEVPERF" -u -q 1 -o 4096 -w randread -t "$RUNTIME" -c /tmp/ma_spdk_malloc.json 2>/dev/null | parse Malloc0)
echo "spdk_malloc,0,$(echo "$r"|cut -d, -f2),$(echo "$r"|cut -d, -f1)" >> "$OUT"

echo "[*] SPDK Delay0 (3 µs, matches null_blk)"
r=$("$BDEVPERF" -u -q 1 -o 4096 -w randread -t "$RUNTIME" -c /tmp/ma_spdk_delay.json -j /tmp/ma_spdk_job.ini 2>/dev/null | parse Delay0)
echo "spdk_delay,3,$(echo "$r"|cut -d, -f2),$(echo "$r"|cut -d, -f1)" >> "$OUT"

echo "[*] wrote $OUT"; column -t -s, "$OUT"
