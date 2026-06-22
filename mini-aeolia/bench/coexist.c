// coexist.c — Test T2: coordinated scheduling under contention.
//
// Models Aeolia §9.3: one latency-critical (LC) I/O thread shares a single CPU
// with N compute-bound "hog" threads. The LC thread issues blocking 4 KB reads,
// so after each completion it must be re-scheduled onto the contended CPU. Under
// the default scheduler it waits behind a hog's time slice -> high tail latency.
// Under the miniaeo coordinated scheduler the LC thread (comm "mlc_io") preempts
// the hog and runs immediately -> low tail latency.
//
//   sudo ./coexist --dev /dev/nullb0 --hogs 3 --cpu 3 --iters 100000 --csv default
//
// The LC thread is named "mlc_io" so the sched_ext policy can recognise it.

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
#include <pthread.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <liburing.h>

static volatile int hogs_stop = 0;
static int g_cpu = 3;

static inline uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}
static void pin(int cpu) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    sched_setaffinity(0, sizeof(s), &s);
}
static uint64_t dev_blocks(int fd, size_t bs) {
    uint64_t sz = 0; struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) sz = st.st_size;
    else ioctl(fd, BLKGETSIZE64, &sz);
    return sz / bs;
}

static void *hog_fn(void *arg) {
    (void)arg;
    pthread_setname_np(pthread_self(), "hog");
    pin(g_cpu);
    volatile uint64_t x = 0;
    while (!hogs_stop) { for (int i = 0; i < 100000; i++) x += i * 2654435761u; }
    return (void *)(uintptr_t)x;
}

struct lc_args { const char *dev; size_t bs; long iters; long warmup; uint64_t *lat; };

static void *lc_fn(void *a) {
    struct lc_args *A = a;
    pthread_setname_np(pthread_self(), "mlc_io");   // <16 chars; sched_ext keys on "mlc"
    pin(g_cpu);

    int fd = open(A->dev, O_RDONLY | O_DIRECT);
    if (fd < 0) { fprintf(stderr, "open: %s\n", strerror(errno)); _exit(1); }
    uint64_t nblk = dev_blocks(fd, A->bs);
    void *buf; if (posix_memalign(&buf, 4096, A->bs)) _exit(1);

    struct io_uring ring;
    if (io_uring_queue_init(8, &ring, 0) < 0) _exit(1);

    uint64_t rng = 0x2545f4914f6cdd1dull;
    long total = A->warmup + A->iters;
    for (long i = 0; i < total; i++) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        uint64_t off = (rng % nblk) * A->bs;
        uint64_t t0 = now_ns();
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, fd, buf, A->bs, off);
        io_uring_submit_and_wait(&ring, 1);          // BLOCK -> sleeps the LC thread
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        io_uring_cqe_seen(&ring, cqe);
        uint64_t dt = now_ns() - t0;
        if (i >= A->warmup) A->lat[i - A->warmup] = dt;
    }
    io_uring_queue_exit(&ring);
    close(fd); free(buf);
    return NULL;
}

int main(int argc, char **argv) {
    const char *dev = "/dev/nullb0";
    int nhogs = 3; long iters = 100000, warmup = 5000; size_t bs = 4096;
    const char *label = "default"; int csv = 0;

    static struct option o[] = {
        {"dev",required_argument,0,'d'},{"hogs",required_argument,0,'H'},
        {"cpu",required_argument,0,'c'},{"iters",required_argument,0,'n'},
        {"bs",required_argument,0,'b'},{"csv",required_argument,0,'C'},{0,0,0,0}};
    int c;
    while ((c = getopt_long(argc, argv, "d:H:c:n:b:C:", o, NULL)) != -1) {
        switch (c) {
        case 'd': dev = optarg; break;
        case 'H': nhogs = atoi(optarg); break;
        case 'c': g_cpu = atoi(optarg); break;
        case 'n': iters = strtol(optarg, NULL, 0); break;
        case 'b': bs = strtoul(optarg, NULL, 0); break;
        case 'C': label = optarg; csv = 1; break;
        default: return 2;
        }
    }

    uint64_t *lat = calloc(iters, sizeof(uint64_t));
    pthread_t hog[64]; if (nhogs > 64) nhogs = 64;
    for (int i = 0; i < nhogs; i++) pthread_create(&hog[i], NULL, hog_fn, NULL);

    struct lc_args A = { dev, bs, iters, warmup, lat };
    pthread_t lc; pthread_create(&lc, NULL, lc_fn, &A);
    pthread_join(lc, NULL);

    hogs_stop = 1;
    for (int i = 0; i < nhogs; i++) pthread_join(hog[i], NULL);

    qsort(lat, iters, sizeof(uint64_t), cmp_u64);
    uint64_t med = lat[iters/2], p99 = lat[(long)(iters*0.99)],
             p999 = lat[(long)(iters*0.999)], p9999 = lat[(long)(iters*0.9999)];

    if (csv) {
        // config,hogs,cpu,median_ns,p99_ns,p999_ns,p9999_ns
        printf("%s,%d,%d,%lu,%lu,%lu,%lu\n", label, nhogs, g_cpu, med, p99, p999, p9999);
    } else {
        printf("config=%s hogs=%d cpu=%d  median=%lu p99=%lu p99.9=%lu p99.99=%lu (ns)\n",
               label, nhogs, g_cpu, med, p99, p999, p9999);
    }
    free(lat);
    return 0;
}
