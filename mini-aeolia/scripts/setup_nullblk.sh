#!/usr/bin/env bash
# setup_nullblk.sh — create a low-latency null_blk device that approximates the
# paper's worst-case condition (an ultra-low-latency SSD like Optane), so that
# interrupt/scheduler overhead is a visible fraction of total latency.
#
# A consumer NVMe completes in tens of microseconds, which masks the effect. A
# null_blk device with a configurable completion delay (3 us by default, in the
# Optane ballpark) makes the effect measurable on commodity hardware.
#
# Requires root (modprobe + configfs). Run:  sudo scripts/setup_nullblk.sh
#
# Env overrides:
#   COMPLETION_NSEC   per-request completion delay in ns         (default 3000)
#   POLL_QUEUES       nr of poll queues (needed for io_uring poll) (default 4)
#   SUBMIT_QUEUES     nr of submit queues                          (default 4)
#   SIZE_MB           device size in MB                            (default 4096)
set -euo pipefail

COMPLETION_NSEC="${COMPLETION_NSEC:-3000}"
POLL_QUEUES="${POLL_QUEUES:-4}"
SUBMIT_QUEUES="${SUBMIT_QUEUES:-4}"
SIZE_MB="${SIZE_MB:-4096}"
# irqmode: 0 inline, 1 softirq, 2 hrtimer. The default is 2, so that completion
# is asynchronous after completion_nsec and the submitting task really sleeps
# and is woken. That wakeup path is what the first finding of the paper is
# about; with irqmode=0 the task never blocks and the effect disappears.
IRQMODE="${IRQMODE:-2}"
NAME="nullb0"

if [[ $EUID -ne 0 ]]; then echo "must run as root (sudo)"; exit 1; fi

# Load module with configfs support (queue_mode=2 => multi-queue).
modprobe null_blk nr_devices=0 || true

CFG=/sys/kernel/config/nullb
DEV="$CFG/$NAME"

if [[ -d "$DEV" ]]; then
    echo "[*] $NAME already exists; removing first"
    echo 0 > "$DEV/power" 2>/dev/null || true
    rmdir "$DEV" 2>/dev/null || true
fi

mkdir -p "$DEV"
echo 0          > "$DEV/power"
echo $SIZE_MB   > "$DEV/size"            # MB
echo 4096       > "$DEV/blocksize"
echo 2          > "$DEV/queue_mode"      # 2 = multi-queue (blk-mq)
echo $IRQMODE   > "$DEV/irqmode"         # 0 = none, 1 = softirq, 2 = hrtimer
echo $COMPLETION_NSEC > "$DEV/completion_nsec"
echo $SUBMIT_QUEUES   > "$DEV/submit_queues"
echo $POLL_QUEUES     > "$DEV/poll_queues" 2>/dev/null || echo "[!] poll_queues not settable (older module); iou_poll may be unavailable"
echo 1          > "$DEV/memory_backed"   # back with RAM so reads return data
echo 1          > "$DEV/power"           # bring device up

# Discover the assigned block device name.
INDEX=$(cat "$DEV/index")
BLK="/dev/nullb${INDEX}"
echo "[*] created $BLK  (completion=${COMPLETION_NSEC}ns size=${SIZE_MB}MB poll_queues=${POLL_QUEUES})"
ls -l "$BLK"
if [[ "$INDEX" != 0 ]]; then
    echo "[!] this is $BLK, not /dev/nullb0: another null_blk device already exists."
    echo "[!] the run_*.sh scripts default to /dev/nullb0; pass $BLK explicitly."
fi
echo "$BLK"
