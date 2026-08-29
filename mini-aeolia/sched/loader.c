// loader.c — load and attach the miniaeo sched_ext scheduler, then stay alive
// (the struct_ops scheduler is active only while this process holds the link).
//
//   ./miniaeo_loader [mode]    mode = 0 fifo, 1 preempt, 2 fair (default 2)
//
// Run it in the background, run the workload, then SIGTERM it to restore the
// default kernel scheduler. It prints READY on stdout once the scheduler is
// attached, and exits non-zero if the kernel ejects the scheduler afterwards.
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <bpf/libbpf.h>
typedef uint64_t u64;   /* the generated skeleton mirrors BPF globals as u64 */
typedef uint32_t u32;
#include "miniaeo.skel.h"

#define SCX_EXIT_ERROR 1024     /* enum scx_exit_kind: >= this means a failure */

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static int libbpf_print(enum libbpf_print_level lvl, const char *fmt, va_list ap)
{
    if (lvl == LIBBPF_DEBUG) return 0;
    return vfprintf(stderr, fmt, ap);
}

int main(int argc, char **argv)
{
    int mode = (argc > 1) ? atoi(argv[1]) : 2;   /* 0 fifo, 1 preempt, 2 fair */
    int rc = 0;

    if (mode < 0 || mode > 2) {
        fprintf(stderr, "bad mode %d (expected 0 fifo, 1 preempt, 2 fair)\n", mode);
        return 2;
    }

    libbpf_set_print(libbpf_print);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    struct miniaeo *skel = miniaeo__open();
    if (!skel) { fprintf(stderr, "open failed\n"); return 1; }

    skel->rodata->mode = mode;

    if (miniaeo__load(skel)) { fprintf(stderr, "load failed\n"); goto err; }

    struct bpf_link *link = bpf_map__attach_struct_ops(skel->maps.miniaeo_ops);
    if (!link) { fprintf(stderr, "attach failed (need root + sched_ext)\n"); goto err; }

    const char *mname = mode == 0 ? "fifo" : mode == 1 ? "preempt" : "fair";
    fprintf(stderr, "[miniaeo] attached (mode=%d/%s). Ctrl-C / SIGTERM to detach.\n",
            mode, mname);
    printf("READY\n"); fflush(stdout);

    while (!stop) {
        sleep(1);
        if (!skel->bss) continue;
        if (skel->bss->exit_kind >= SCX_EXIT_ERROR) {
            fprintf(stderr, "\n[miniaeo] kernel ejected the scheduler (kind=%u): %s\n",
                    skel->bss->exit_kind, skel->bss->exit_msg);
            rc = 1;
            break;
        }
        fprintf(stderr, "[miniaeo] preempt=%llu global=%llu vtime=%llu\r",
                (unsigned long long)skel->bss->nr_preempt,
                (unsigned long long)skel->bss->nr_global,
                (unsigned long long)skel->bss->nr_vtime);
    }
    fprintf(stderr, "\n[miniaeo] detaching\n");
    bpf_link__destroy(link);
    miniaeo__destroy(skel);
    return rc;
err:
    miniaeo__destroy(skel);
    return 1;
}
