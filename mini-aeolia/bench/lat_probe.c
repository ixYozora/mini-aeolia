// lat_probe.c — single-task, queue-depth-1 read-latency probe.
//
// Reproduces the methodology behind Aeolia's Finding #1 (paper §2): compare the
// per-request latency of a single task issuing 4 KB reads across three I/O
// completion paths:
//
//   posix      kernel stack, interrupt-driven   (pread, O_DIRECT)
//   iou        io_uring default, interrupt       (the userspace+interrupt point)
//   iou_poll   io_uring with IORING_SETUP_IOPOLL (userspace+polling)
//
// The gap between iou and iou_poll is the "interrupt overhead" the paper
// decomposes — most of which it attributes to the scheduler's idle-task dance,
// not the interrupt mechanism itself. Run this under the default scheduler and
// again under the active-checking sched_ext scheduler (M2) to see the gap shrink.
//
// Build:  see ../Makefile   (needs liburing)
// Usage:  sudo ./lat_probe --dev /dev/nullb0 --mode iou --bs 4096 --iters 200000
//
// Notes:
//  * O_DIRECT requires the buffer, offset, and size to be 512-byte aligned; we
//    align everything to 4096.
//  * Reads are issued to random aligned offsets to defeat any caching, though
//    O_DIRECT already bypasses the page cache.
//  * Latency is measured with CLOCK_MONOTONIC around a single in-flight request
//    (queue depth 1), matching the paper's single-task scenario.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <linux/fs.h>
#include <liburing.h>

enum mode { M_POSIX, M_IOU, M_IOU_POLL, M_IOU_ACTIVE };

static const char *mode_name(enum mode m) {
    switch (m) { case M_POSIX: return "posix";
                 case M_IOU: return "iou";
                 case M_IOU_POLL: return "iou_poll";
                 default: return "iou_active"; }
}

// Userspace realization of Aeolia's "active-checking policy" (paper §2,
// Finding #1): instead of blocking in io_uring_enter (which puts the task to
// sleep and triggers the kernel idle-task dance when no other task is
// runnable), submit the request and spin on the completion queue for a bounded
// budget before falling back to a blocking wait. This keeps the task on-CPU
// across the device latency window, removing the scheduler overhead — the same
// effect Aeolia's iou_opt achieves.
static inline void cpu_pause(void) { __asm__ __volatile__("pause" ::: "memory"); }

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static inline uint64_t tv2ns(struct timeval tv) {
    return (uint64_t)tv.tv_sec * 1000000000ull + (uint64_t)tv.tv_usec * 1000ull;
}

static uint64_t dev_size(int fd) {
    uint64_t sz = 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) return (uint64_t)st.st_size;
    if (ioctl(fd, BLKGETSIZE64, &sz) == 0) return sz;
    return 0;
}

static void pin_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        perror("sched_setaffinity (non-fatal)");
}

int main(int argc, char **argv) {
    const char *dev = "/dev/nullb0";
    enum mode m = M_IOU;
    size_t bs = 4096;
    long iters = 200000;
    long warmup = 5000;
    int cpu = 2;
    int csv = 0;
    long spin_us = 50;   // active-checking spin budget (microseconds)

    static struct option opts[] = {
        {"dev",    required_argument, 0, 'd'},
        {"mode",   required_argument, 0, 'm'},
        {"bs",     required_argument, 0, 'b'},
        {"iters",  required_argument, 0, 'n'},
        {"warmup", required_argument, 0, 'w'},
        {"cpu",    required_argument, 0, 'c'},
        {"spin",   required_argument, 0, 's'},
        {"csv",    no_argument,       0, 'C'},
        {0,0,0,0}
    };
    int o;
    while ((o = getopt_long(argc, argv, "d:m:b:n:w:c:s:C", opts, NULL)) != -1) {
        switch (o) {
        case 'd': dev = optarg; break;
        case 'm':
            if (!strcmp(optarg, "posix")) m = M_POSIX;
            else if (!strcmp(optarg, "iou")) m = M_IOU;
            else if (!strcmp(optarg, "iou_poll")) m = M_IOU_POLL;
            else if (!strcmp(optarg, "iou_active")) m = M_IOU_ACTIVE;
            else { fprintf(stderr, "bad mode %s\n", optarg); return 2; }
            break;
        case 's': spin_us = strtol(optarg, NULL, 0); break;
        case 'b': bs = strtoul(optarg, NULL, 0); break;
        case 'n': iters = strtol(optarg, NULL, 0); break;
        case 'w': warmup = strtol(optarg, NULL, 0); break;
        case 'c': cpu = atoi(optarg); break;
        case 'C': csv = 1; break;
        default: return 2;
        }
    }

    pin_cpu(cpu);

    int flags = O_RDONLY | O_DIRECT;
    int fd = open(dev, flags);
    if (fd < 0) { fprintf(stderr, "open(%s): %s\n", dev, strerror(errno)); return 1; }

    uint64_t sz = dev_size(fd);
    if (sz < bs) { fprintf(stderr, "device too small or unknown size\n"); return 1; }
    uint64_t nblocks = sz / bs;

    void *buf = NULL;
    if (posix_memalign(&buf, 4096, bs)) { perror("posix_memalign"); return 1; }

    struct io_uring ring;
    int use_ring = (m != M_POSIX);
    if (use_ring) {
        unsigned rflags = (m == M_IOU_POLL) ? IORING_SETUP_IOPOLL : 0;
        int r = io_uring_queue_init(8, &ring, rflags);
        if (r < 0) { fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-r)); return 1; }
    }

    long total = warmup + iters;
    uint64_t *lat = calloc(iters, sizeof(uint64_t));
    if (!lat) { perror("calloc"); return 1; }

    // simple xorshift for offsets
    uint64_t rng = 0x9e3779b97f4a7c15ull ^ (uint64_t)getpid();

    // CPU-utilization accounting over the measured region (for active-checking
    // cost analysis, experiment O1): spinning keeps the core busy even while
    // waiting, so latency gains trade against CPU utilization.
    struct rusage ru0; uint64_t wall0 = 0;

    for (long i = 0; i < total; i++) {
        if (i == warmup) { getrusage(RUSAGE_THREAD, &ru0); wall0 = now_ns(); }
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        uint64_t off = (rng % nblocks) * bs;

        uint64_t t0 = now_ns();
        if (m == M_POSIX) {
            ssize_t got = pread(fd, buf, bs, (off_t)off);
            if (got != (ssize_t)bs) { fprintf(stderr, "pread: %s\n", strerror(errno)); return 1; }
        } else if (m == M_IOU_ACTIVE) {
            // active-checking: submit, then spin on the CQ before blocking
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe, fd, buf, bs, off);
            int s = io_uring_submit(&ring);
            if (s < 0) { fprintf(stderr, "submit: %s\n", strerror(-s)); return 1; }
            struct io_uring_cqe *cqe = NULL;
            uint64_t deadline = t0 + (uint64_t)spin_us * 1000ull;
            int got = 0;
            do {
                if (io_uring_peek_cqe(&ring, &cqe) == 0 && cqe) { got = 1; break; }
                cpu_pause();
            } while (now_ns() < deadline);
            if (!got) {                                  // budget exhausted -> block
                int r = io_uring_wait_cqe(&ring, &cqe);
                if (r < 0) { fprintf(stderr, "wait_cqe: %s\n", strerror(-r)); return 1; }
            }
            if (cqe->res < 0) { fprintf(stderr, "io read: %s\n", strerror(-cqe->res)); return 1; }
            io_uring_cqe_seen(&ring, cqe);
        } else {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe, fd, buf, bs, off);
            int s = io_uring_submit_and_wait(&ring, 1);
            if (s < 0) { fprintf(stderr, "submit_and_wait: %s\n", strerror(-s)); return 1; }
            struct io_uring_cqe *cqe;
            int r = io_uring_wait_cqe(&ring, &cqe);
            if (r < 0) { fprintf(stderr, "wait_cqe: %s\n", strerror(-r)); return 1; }
            if (cqe->res < 0) { fprintf(stderr, "io read: %s\n", strerror(-cqe->res)); return 1; }
            io_uring_cqe_seen(&ring, cqe);
        }
        uint64_t dt = now_ns() - t0;
        if (i >= warmup) lat[i - warmup] = dt;
    }

    struct rusage ru1; getrusage(RUSAGE_THREAD, &ru1);
    uint64_t wall1 = now_ns();
    double cpu_ns = (double)(tv2ns(ru1.ru_utime) + tv2ns(ru1.ru_stime)
                           - tv2ns(ru0.ru_utime) - tv2ns(ru0.ru_stime));
    double cpu_util = (wall1 > wall0) ? cpu_ns / (double)(wall1 - wall0) : 0.0;

    qsort(lat, iters, sizeof(uint64_t), cmp_u64);
    uint64_t sum = 0;
    for (long i = 0; i < iters; i++) sum += lat[i];
    double mean = (double)sum / iters;
    uint64_t med  = lat[iters / 2];
    uint64_t p99  = lat[(long)(iters * 0.99)];
    uint64_t p999 = lat[(long)(iters * 0.999)];
    uint64_t mn   = lat[0];
    double iops = 1e9 / mean;

    if (csv) {
        // mode,bs,iters,min_ns,median_ns,mean_ns,p99_ns,p999_ns,iops,cpu_util
        printf("%s,%zu,%ld,%lu,%lu,%.1f,%lu,%lu,%.0f,%.3f\n",
               mode_name(m), bs, iters, mn, med, mean, p99, p999, iops, cpu_util);
    } else {
        printf("mode=%-8s bs=%zu dev=%s cpu=%d iters=%ld spin=%ldus\n",
               mode_name(m), bs, dev, cpu, iters, spin_us);
        printf("  min      = %6lu ns\n", mn);
        printf("  median   = %6lu ns\n", med);
        printf("  mean     = %6.1f ns\n", mean);
        printf("  p99      = %6lu ns\n", p99);
        printf("  p99.9    = %6lu ns\n", p999);
        printf("  IOPS     = %6.0f (depth=1, single task)\n", iops);
        printf("  cpu_util = %6.1f%% (busy fraction of the core)\n", cpu_util * 100.0);
    }

    free(lat);
    free(buf);
    if (use_ring) io_uring_queue_exit(&ring);
    close(fd);
    return 0;
}
