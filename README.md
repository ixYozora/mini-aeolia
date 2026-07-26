# mini-aeolia

A userspace storage testbed that reproduces the central findings of
[**Aeolia: A Fast and Secure Userspace Interrupt-Based Storage Stack**](https://doi.org/10.1145/3731569.3764816)
(SOSP '25) on commodity hardware.

Aeolia depends on two things an ordinary machine does not have: **user
interrupts** (UINTR, Intel Sapphire Rapids and newer) and an **Optane-class
SSD**. mini-aeolia substitutes both with widely available equivalents and asks
whether the paper's *claims* still show up. They do, qualitatively.

> **This is not Aeolia and does not reimplement it.** It replaces Aeolia's two
> defining mechanisms (user interrupts and MPK-protected sharing) with commodity
> stand-ins. It reproduces the paper's findings in direction and order of
> magnitude, not in absolute numbers.

## What is substituted

| Aeolia | mini-aeolia |
| --- | --- |
| User interrupts (UINTR) | io_uring completion (`iou`) plus an active-checking path (`iou_active`) |
| Optane SSD | `null_blk` with a tunable completion delay (default 3 µs) |
| Coordinated sched_ext scheduler | `miniaeo`, an sched_ext (eBPF) scheduler written for this testbed |
| AeoFS library filesystem | `mfs`, a library filesystem on io_uring with a write-back block cache |
| MPK-protected sharing | not modelled (single trust domain) |
| Real kernel bypass | SPDK against a RAM bdev, `--no-pci` |

## Results

**The cost of an interrupt is scheduling, not delivery** (T1, 4 KB read, queue
depth 1, median latency in µs):

| `posix` | `iou` (interrupt) | `iou_active` | `iou_poll` |
| --- | --- | --- | --- |
| 6.78 | 7.36 | 5.32 | 1.37 |

Replacing the sleep/wake with active checking removes about 2 µs without
changing the delivery mechanism. At 128 KB the paths converge, because device
transfer time dominates.

**Coordinated scheduling collapses tail latency** (T2, one latency-critical
thread sharing a core with 3 compute hogs, p99.9):

| default (EEVDF) | `scx_fifo` (custom scheduler, no LC awareness) | `scx_coord` (LC-aware) |
| --- | --- | --- |
| 3.00 ms | 6.04 ms | 27 µs |

The `scx_fifo` control run stays in the millisecond range, so the 111× win comes
from the policy and not from merely swapping the scheduler out.

**A library filesystem beats ext4 on metadata once it has a cache** (T3b, 5000
files of 4 KB on the same device, ops/s):

| op | `mfs` (cache) | `ext4` | `f2fs` | `mfs` vs `ext4` |
| --- | --- | --- | --- | --- |
| create | 744,348 | 28,164 | 50,161 | 26× |
| read | 1,380,193 | 473,217 | 773,338 | 2.9× |
| stat | 7,613,676 | 1,256,211 | 1,518,273 | 6.1× |
| write | 152,949 | 181,460 | 262,264 | 0.84× |

Write stays behind because `mfs` zero-fills every newly allocated block.

**Latency and throughput cannot both be won in software** (T2c, p99.9 latency
and compute batches/s at 4 hogs):

| policy | p99.9 | compute batches/s |
| --- | --- | --- |
| default | 3.71 ms | 2698 |
| `preempt` (coordinated) | 27.7 µs | 247 |
| `fair` (weighted vtime) | 8.00 ms | 3363 |

Fairness does not remove the cost of prioritisation, it moves it from throughput
to latency. Making prioritisation cheap enough to avoid the trade-off is exactly
what Aeolia's user interrupts buy.

## Requirements

- x86-64 Linux, **kernel 6.12 or newer with sched_ext enabled** (only the
  scheduler experiments need it):
  ```sh
  grep CONFIG_SCHED_CLASS_EXT /boot/config-$(uname -r)   # expect =y
  ```
- **root** (module loading, configfs, `mkfs`, `mount`)
- about 4 GB of free RAM for the memory-backed `null_blk` device

Debian/Ubuntu packages:

```sh
sudo apt install -y build-essential pkg-config \
    liburing-dev clang llvm libbpf-dev \
    linux-tools-common linux-tools-$(uname -r) \
    fio jq e2fsprogs f2fs-tools \
    python3-matplotlib python3-numpy python3-pandas
```

`linux-tools-*` provides `bpftool`, which generates `vmlinux.h` and the BPF
skeleton for the scheduler.

## Quickstart

```sh
git clone https://github.com/ixYozora/mini-aeolia.git
cd mini-aeolia/mini-aeolia        # all commands below run from here

make                              # benchmark binaries  -> bin/
make -C sched                     # sched_ext scheduler -> sched/miniaeo_loader

sudo scripts/setup_nullblk.sh     # -> /dev/nullb0, 3 µs completion delay
sudo scripts/run_all.sh           # build, run everything, plot -> results/
```

`run_all.sh` runs T1 through T5 and plots them. It skips the optional SPDK step
if SPDK is not built, and it leaves out `run_o*.sh`, because `run_o2.sh`
recreates the device at different latencies and would disturb the other runs.
Run those separately afterwards.

> **Careful:** the filesystem tests run `mkfs` on the device they are given and
> mount it at `/mnt/mfs_ext4`. Always pass the `null_blk` device. Pointing them
> at a real disk destroys its contents.

## Repository layout

```
mini-aeolia/
  bench/    lat_probe.c   single-task latency probe            (T1)
            coexist.c     LC thread + compute hogs             (T2)
            fs_micro.c    mfs vs kernel filesystems            (T3)
  libfs/    mfs.c/.h      library filesystem on io_uring, write-back cache
  sched/    miniaeo.bpf.c sched_ext scheduler (fifo/preempt/fair)
            loader.c      attaches it and holds the link open
  scripts/  setup_nullblk.sh, run_*.sh    experiment drivers -> results/*.csv
  plot/     plot_*.py                     CSV -> results/*.png
  results/  CSVs and figures from the runs reported above
```

## Experiments

Every script takes the device as its first argument and defaults to
`/dev/nullb0`. All of them need root.

| Script | Experiment | Output |
| --- | --- | --- |
| `setup_nullblk.sh` | create the low-latency device | `/dev/nullb0` |
| `run_t1.sh` | latency decomposition across 4 I/O paths | `t1_latency.csv` |
| `run_t1_depth.sh` | queue-depth sweep, interrupt vs polling (fio) | `t1_depth.csv` |
| `run_t2.sh` | tail latency under contention, 3 schedulers | `t2_coexist.csv` |
| `run_sched.sh` | default vs preempt vs fair, latency **and** throughput | `sched_policies.csv` |
| `run_t3.sh` | `mfs` vs `ext4`, no cache | `t3_fs_micro.csv` |
| `run_t3_cache.sh` | `mfs` cached/uncached vs `ext4`/`f2fs` | `t3b_cache.csv` |
| `run_baselines.sh` | fio storage baselines | `t5_fio_baselines.csv` |
| `run_o1.sh` | spin-budget sweep: latency vs CPU utilisation | `o1_spin_sweep.csv` |
| `run_o2.sh` | device-latency sweep, 1 µs to 50 µs | `o2_devlat_sweep.csv` |
| `run_o3.sh` | contention sweep, latency and hog throughput | `o3_contention.csv` |
| `run_m4_spdk.sh` | SPDK kernel bypass (optional, see below) | `t4_spdk.csv` |
| `run_leveldb.sh` | LevelDB `db_bench` on ext4 vs f2fs (optional) | `t6_leveldb.csv` |

Tunables are environment variables, for example:

```sh
COMPLETION_NSEC=20000 sudo scripts/setup_nullblk.sh   # 20 µs device instead of 3 µs
ITERS=50000 sudo scripts/run_t1.sh                    # shorter run
HOGS=8 sudo scripts/run_t2.sh                         # heavier contention
```

Figures are regenerated from the CSVs without root:

```sh
python3 plot/plot_t1.py        # and plot_t2, plot_t3, plot_t3b, plot_sched, ...
```

### The scheduler

`sched/miniaeo_loader <mode>` attaches the scheduler and keeps it active until it
receives `SIGTERM`. Modes are `0` fifo (no LC awareness, used as a control), `1`
preempt (the coordinated policy) and `2` fair (weighted virtual time).

### Optional components

Both are skipped unless you point the scripts at an existing build:

```sh
SPDK_DIR=/path/to/spdk       sudo -E scripts/run_m4_spdk.sh
DB_BENCH=/path/to/db_bench   sudo -E scripts/run_leveldb.sh
```

`run_m4_spdk.sh` never calls SPDK's `setup.sh`. That script would bind every NVMe
controller including the boot disk to VFIO. It allocates hugepages itself and
runs `bdevperf` with `--no-pci` against RAM-backed bdevs.

## Limits

- MPK-protected sharing and the permission table are **not** modelled. There is a
  single trust domain, so the security properties of Aeolia are out of scope.
- User interrupts are not modelled. `iou_active` approximates their effect on
  latency by removing the sleep/wake, but it burns CPU to do so.
- `null_blk` is a RAM device with an artificial delay. It has no controller
  queueing, no garbage collection and no write amplification.
- Absolute numbers are not comparable to the paper, which used 128 cores and a
  real Optane SSD.
- `mfs` has no journal and no crash consistency, and it has not been tested
  beyond the workloads in `bench/`.
