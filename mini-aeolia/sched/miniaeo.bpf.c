// miniaeo.bpf.c — sched_ext scheduler with three selectable policies, used to
// study Aeolia's coordinated scheduling (paper §6) on commodity hardware.
//
//   mode 0  fifo     every task in the global FIFO, no LC awareness (control run)
//   mode 1  preempt  LC tasks (comm starting with "mlc") preempt the running
//                    task. Blunt priority: low LC latency, low compute throughput.
//   mode 2  fair     weighted virtual time in the spirit of EEVDF, with wakeup
//                    preemption for eligible tasks.
//
// The loader passes the mode as its argument. run_t2.sh compares the default
// kernel scheduler against modes 0 and 1; run_sched.sh compares it against
// modes 1 and 2.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define SLICE_DFL   1000000ULL    /* 1 ms, fifo and preempt modes */
#define SLICE_FAIR   100000ULL    /* 100 us; a short slice bounds the LC wait */
#define SHARED_DSQ  0

#define BPF_STRUCT_OPS(name, args...) \
    SEC("struct_ops/" #name) BPF_PROG(name, ##args)
#define BPF_STRUCT_OPS_SLEEPABLE(name, args...) \
    SEC("struct_ops.s/" #name) BPF_PROG(name, ##args)

const volatile int mode = 2;        /* set by the loader: 0 fifo, 1 preempt, 2 fair */

extern s32  scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, bool *is_idle) __ksym;
extern void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym;
extern void scx_bpf_dsq_insert_vtime(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym;
extern bool scx_bpf_dsq_move_to_local(u64 dsq_id) __ksym;
extern s32  scx_bpf_create_dsq(u64 dsq_id, s32 node) __ksym;
extern void scx_bpf_kick_cpu(s32 cpu, u64 flags) __ksym;
extern s32  scx_bpf_task_cpu(const struct task_struct *p) __ksym;

u64 nr_preempt = 0, nr_global = 0, nr_vtime = 0;
u32 exit_kind = 0;                  /* != 0 once the kernel has ejected us */
char exit_msg[128];

static u64 vtime_now;

/* The slice a task is given must match the budget stopping() charges against,
   otherwise the subtraction there underflows and the task is starved. */
static __always_inline u64 slice_len(void)
{
    return mode == 2 ? SLICE_FAIR : SLICE_DFL;
}

static inline bool vtime_before(u64 a, u64 b) { return (s64)(a - b) < 0; }
static __always_inline bool is_lc(struct task_struct *p)
{ return p->comm[0] == 'm' && p->comm[1] == 'l' && p->comm[2] == 'c'; }

s32 BPF_STRUCT_OPS(miniaeo_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle)
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice_len(), 0);
    return cpu;
}

void BPF_STRUCT_OPS(miniaeo_enqueue, struct task_struct *p, u64 enq_flags)
{
    if (mode == 2) {                                   /* fair: weighted vtime */
        u64 vtime = p->scx.dsq_vtime;
        /* a long-sleeping task may not hoard credit: bound the bonus to a slice */
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
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SLICE_DFL, enq_flags | SCX_ENQ_PREEMPT);
    } else {                                           /* fifo, and non-LC tasks */
        __sync_fetch_and_add(&nr_global, 1);
        scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SLICE_DFL, enq_flags);
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
        u64 left = p->scx.slice, used;
        /* charge the runtime just used, scaled inversely by weight */
        used = left < SLICE_FAIR ? SLICE_FAIR - left : SLICE_FAIR;
        p->scx.dsq_vtime += used * 100 / w;
    }
}

s32 BPF_STRUCT_OPS_SLEEPABLE(miniaeo_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

/* Record why the scheduler was unloaded. Without this a scheduler ejected by
   the kernel mid-run is invisible, and the results are labelled as if it had
   still been attached. */
void BPF_STRUCT_OPS(miniaeo_exit, struct scx_exit_info *ei)
{
    bpf_probe_read_kernel_str(exit_msg, sizeof(exit_msg), ei->msg);
    exit_kind = ei->kind;
}

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
