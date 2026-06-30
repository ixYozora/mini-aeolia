// miniaeo.bpf.c — a sched_ext scheduler with three selectable policies, to
// study Aeolia's coordinated scheduling (paper §6) on commodity hardware.
//
//   mode 0  fifo    : everyone in the global FIFO (no LC awareness)  — control
//   mode 1  preempt : LC tasks (comm "mlc") ALWAYS preempt the running task
//                     — blunt priority; great LC latency, but starves compute (O4)
//   mode 2  fair    : weighted virtual-time scheduling (EEVDF/CFS-like) with
//                     wakeup preemption for eligible tasks — the policy Aeolia
//                     actually uses (it reimplements EEVDF). Aims for low LC
//                     latency WITHOUT the throughput collapse.
//
// The loader sets `mode`. T2/O3/O4 compare default(no scx) / preempt / fair.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define SCX_SLICE_DFL 1000000ULL    /* 1 ms (preempt/fifo modes) */
#define SLICE_FAIR     100000ULL    /* 100 us — short slice bounds LC wait in fair mode */
#define SHARED_DSQ    0

#define BPF_STRUCT_OPS(name, args...) \
    SEC("struct_ops/" #name) BPF_PROG(name, ##args)
#define BPF_STRUCT_OPS_SLEEPABLE(name, args...) \
    SEC("struct_ops.s/" #name) BPF_PROG(name, ##args)

const volatile int mode = 2;        /* set by loader: 0 fifo, 1 preempt, 2 fair */

extern s32  scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, bool *is_idle) __ksym;
extern void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym;
extern void scx_bpf_dsq_insert_vtime(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym;
extern bool scx_bpf_dsq_move_to_local(u64 dsq_id) __ksym;
extern s32  scx_bpf_create_dsq(u64 dsq_id, s32 node) __ksym;
extern void scx_bpf_kick_cpu(s32 cpu, u64 flags) __ksym;
extern s32  scx_bpf_task_cpu(const struct task_struct *p) __ksym;

u64 nr_preempt = 0, nr_global = 0, nr_vtime = 0;
static u64 vtime_now;

static inline bool vtime_before(u64 a, u64 b) { return (s64)(a - b) < 0; }
static __always_inline bool is_lc(struct task_struct *p)
{ return p->comm[0] == 'm' && p->comm[1] == 'l' && p->comm[2] == 'c'; }

s32 BPF_STRUCT_OPS(miniaeo_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle)
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
    return cpu;
}

void BPF_STRUCT_OPS(miniaeo_enqueue, struct task_struct *p, u64 enq_flags)
{
    if (mode == 2) {                                   /* fair: weighted vtime */
        u64 vtime = p->scx.dsq_vtime;
        /* a long-sleeping task may not hoard credit (bound the bonus to 1 slice) */
        if (vtime_before(vtime, vtime_now - SLICE_FAIR))
            vtime = vtime_now - SLICE_FAIR;
        scx_bpf_dsq_insert_vtime(p, SHARED_DSQ, SLICE_FAIR, vtime, enq_flags);
        __sync_fetch_and_add(&nr_vtime, 1);
        /* EEVDF-style wakeup preemption: a just-woken, eligible task runs now */
        if ((enq_flags & SCX_ENQ_WAKEUP) && vtime_before(vtime, vtime_now))
            scx_bpf_kick_cpu(scx_bpf_task_cpu(p), SCX_KICK_PREEMPT);
        return;
    }
    if (mode == 1 && is_lc(p)) {                       /* blunt always-preempt */
        __sync_fetch_and_add(&nr_preempt, 1);
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, enq_flags | SCX_ENQ_PREEMPT);
    } else {                                           /* fifo / non-LC */
        __sync_fetch_and_add(&nr_global, 1);
        scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
    }
}

void BPF_STRUCT_OPS(miniaeo_dispatch, s32 cpu, struct task_struct *prev)
{
    if (mode == 2)
        scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(miniaeo_running, struct task_struct *p)
{
    if (mode == 2 && vtime_before(vtime_now, p->scx.dsq_vtime))
        vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(miniaeo_stopping, struct task_struct *p, bool runnable)
{
    if (mode == 2) {
        u32 w = p->scx.weight ? p->scx.weight : 100;
        /* charge the runtime just used, scaled inversely by weight */
        p->scx.dsq_vtime += (SLICE_FAIR - p->scx.slice) * 100 / w;
    }
}

s32 BPF_STRUCT_OPS_SLEEPABLE(miniaeo_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ, -1);
}
void BPF_STRUCT_OPS(miniaeo_exit, struct scx_exit_info *ei) {}

SEC(".struct_ops.link")
struct sched_ext_ops miniaeo_ops = {
    .select_cpu = (void *)miniaeo_select_cpu,
    .enqueue    = (void *)miniaeo_enqueue,
    .dispatch   = (void *)miniaeo_dispatch,
    .running    = (void *)miniaeo_running,
    .stopping   = (void *)miniaeo_stopping,
    .init       = (void *)miniaeo_init,
    .exit       = (void *)miniaeo_exit,
    .flags      = 0,
    .name       = "miniaeo",
};
