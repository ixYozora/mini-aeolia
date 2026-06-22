// miniaeo.bpf.c — a minimal sched_ext scheduler demonstrating Aeolia's
// coordinated-scheduling idea (paper §6): a latency-critical (LC) I/O task that
// shares a core with compute-bound tasks should run *immediately* when its I/O
// completes, instead of waiting behind a compute task's time slice.
//
// Policy:
//   * LC tasks (identified by comm prefix "mlc") are dispatched to the CPU's
//     LOCAL queue with SCX_ENQ_PREEMPT -> they preempt the running compute task.
//   * Everything else goes to the built-in GLOBAL queue (plain FIFO).
//
// A `coordinate` flag (set by the loader) toggles the LC priority: with it off,
// LC tasks are treated like everyone else (FIFO) -- this is the "custom
// scheduler but no coordination" control. Compare three configs in T2:
//   (1) default kernel EEVDF  (no scheduler loaded)
//   (2) miniaeo coordinate=0  (sched_ext FIFO, no LC awareness)
//   (3) miniaeo coordinate=1  (LC tasks prioritized -> low tail latency)

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define SCX_SLICE_DFL 1000000ULL    /* 1 ms time slice (scx default is 20 ms);
                                       shorter slice bounds the FIFO control's
                                       latency so T2 completes in finite time */

#define BPF_STRUCT_OPS(name, args...) \
    SEC("struct_ops/" #name) BPF_PROG(name, ##args)
#define BPF_STRUCT_OPS_SLEEPABLE(name, args...) \
    SEC("struct_ops.s/" #name) BPF_PROG(name, ##args)

/* set read-only from userspace before load */
const volatile bool coordinate = true;

/* kfuncs provided by the kernel's sched_ext core */
extern s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu,
                                  u64 wake_flags, bool *is_idle) __ksym;
extern void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice,
                               u64 enq_flags) __ksym;

/* counters surfaced to userspace for sanity/debug */
u64 nr_lc_preempt = 0;
u64 nr_global = 0;

static __always_inline bool is_lc(struct task_struct *p)
{
    /* match comm prefix "mlc" (mini-aeolia latency-critical) */
    return p->comm[0] == 'm' && p->comm[1] == 'l' && p->comm[2] == 'c';
}

s32 BPF_STRUCT_OPS(miniaeo_select_cpu, struct task_struct *p, s32 prev_cpu,
                   u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle) {
        /* idle CPU available: run right here, no contention */
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
    }
    return cpu;
}

void BPF_STRUCT_OPS(miniaeo_enqueue, struct task_struct *p, u64 enq_flags)
{
    if (coordinate && is_lc(p)) {
        /* LC task woke and no idle CPU -> preempt the compute hog now */
        __sync_fetch_and_add(&nr_lc_preempt, 1);
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL,
                           enq_flags | SCX_ENQ_PREEMPT);
    } else {
        __sync_fetch_and_add(&nr_global, 1);
        scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
    }
}

s32 BPF_STRUCT_OPS(miniaeo_init)
{
    return 0;
}

void BPF_STRUCT_OPS(miniaeo_exit, struct scx_exit_info *ei)
{
}

SEC(".struct_ops.link")
struct sched_ext_ops miniaeo_ops = {
    .select_cpu = (void *)miniaeo_select_cpu,
    .enqueue    = (void *)miniaeo_enqueue,
    .init       = (void *)miniaeo_init,
    .exit       = (void *)miniaeo_exit,
    .flags      = 0,
    .name       = "miniaeo",
};
