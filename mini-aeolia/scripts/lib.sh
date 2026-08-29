# lib.sh — helpers shared by the run_*.sh drivers. Sourced, not executed.
#
#   require_scratch_dev <dev>   refuse to mkfs anything but a scratch device
#   sched_start <mode>          attach the scheduler (0 fifo, 1 preempt, 2 fair)
#   sched_check                 abort if the scheduler is no longer attached
#   sched_stop                  detach and restore the kernel scheduler

# The filesystem experiments run mkfs on the device they are given, which
# destroys its contents. Accept only a null_blk device unless the caller
# explicitly overrides that.
require_scratch_dev() {
    local dev="$1"
    [[ -b "$dev" ]] || { echo "$dev is not a block device; run scripts/setup_nullblk.sh"; exit 1; }
    if findmnt -S "$dev" >/dev/null 2>&1; then
        echo "refusing to mkfs $dev: it is mounted"; exit 1
    fi
    if [[ "$dev" != /dev/nullb* && "${ALLOW_ANY_DEV:-0}" != 1 ]]; then
        echo "refusing to mkfs $dev: not a null_blk device, and mkfs destroys"
        echo "everything on it. Set ALLOW_ANY_DEV=1 to override."
        exit 1
    fi
}

LOADER="${LOADER:-sched/miniaeo_loader}"

sched_start() {
    SCHED_READY=$(mktemp)
    SCHED_LOG=$(mktemp)
    "$LOADER" "$1" >"$SCHED_READY" 2>"$SCHED_LOG" &
    SCHED_PID=$!
    for _ in $(seq 1 50); do
        if grep -q READY "$SCHED_READY" 2>/dev/null; then return 0; fi
        if ! kill -0 "$SCHED_PID" 2>/dev/null; then break; fi
        sleep 0.1
    done
    echo "scheduler failed to attach (mode $1):"
    cat "$SCHED_LOG"
    sched_stop
    exit 1
}

# The kernel ejects a misbehaving sched_ext scheduler on its own. Without this
# check a run would be labelled as scheduled by miniaeo when it was not.
sched_check() {
    if kill -0 "$SCHED_PID" 2>/dev/null; then return 0; fi
    echo "scheduler stopped during the run:"
    cat "$SCHED_LOG"
    exit 1
}

sched_stop() {
    if [[ -n "${SCHED_PID:-}" ]]; then
        kill -TERM "$SCHED_PID" 2>/dev/null || true
        wait "$SCHED_PID" 2>/dev/null || true
        unset SCHED_PID
    fi
    rm -f "${SCHED_READY:-}" "${SCHED_LOG:-}"
    sleep 0.3
}
