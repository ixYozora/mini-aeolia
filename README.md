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

The numbers below are one run on an Intel Core i7-10510U (4 cores, 8 threads),
16 GB RAM, Ubuntu 26.04, Linux 7.0.0, against `null_blk` at a 3 µs completion
delay. Absolute values move between runs and between machines; what reproduces
is the ordering and the order of magnitude. Re-running `scripts/run_all.sh`
regenerates every table here.

**The cost of an interrupt is scheduling, not delivery** (T1, 4 KB read, queue
depth 1, median latency in µs):

| `posix` | `iou` (interrupt) | `iou_active` | `iou_poll` |
| --- | --- | --- | --- |
| 8.12 | 8.75 | 5.55 | 1.75 |

Replacing the sleep and wake with active checking removes 3.2 µs without
changing the delivery mechanism, at the cost of running the core at 100 %
utilisation instead of 63 %. At 128 KB the gap falls to 2.3 µs, because device
transfer time dominates.

`iou_poll` is listed for completeness but is not comparable to the other three.
`null_blk` does not apply its configured completion delay on the poll-queue
path: raising `completion_nsec` from 3 µs to 20 µs moves the `iou` median from
8.5 µs to 26.4 µs and leaves the `iou_poll` median at 1.7 µs to 2.0 µs. Every
polling figure in this repository is therefore a floor for the polling stack,
not a measurement at 3 µs.

**Coordinated scheduling collapses tail latency** (T2, one latency-critical
thread sharing a core with 3 compute hogs, p99.9):

| default (EEVDF) | `scx_fifo` (custom scheduler, no LC awareness) | `scx_coord` (LC-aware) |
| --- | --- | --- |
| 3.10 ms | 6.02 ms | 24.3 µs |

The `scx_fifo` control run stays in the millisecond range, so the 127× win comes
from the policy and not from merely swapping the scheduler out.

**A library filesystem beats ext4 on metadata once it has a cache** (T3b, 5000
files of 4 KB on the same device, ops/s):

| op | `mfs` (cache) | `ext4` | `f2fs` | `mfs` vs `ext4` |
| --- | --- | --- | --- | --- |
| create | 870,698 | 39,017 | 56,838 | 22× |
| read | 1,364,798 | 565,819 | 827,057 | 2.4× |
| stat | 7,331,335 | 1,546,468 | 1,571,578 | 4.7× |
| write | 195,755 | 272,057 | 266,685 | 0.72× |

Write stays behind because `mfs` zero-fills every newly allocated block. Without
the cache `mfs` loses on every operation, at 13,799 creates/s against ext4's
39,017, because each one becomes a synchronous device round trip.

**Latency and throughput pull against each other** (T2c). LC p99.9 latency in µs:

| hogs | default | `preempt` | `fair` |
| --- | --- | --- | --- |
| 1 | 835 | 29 | 27 |
| 2 | 3,896 | 23 | 23 |
| 4 | 3,376 | 18 | 14 |
| 8 | 3,698 | 12 | 954 |

Compute throughput over the same runs, in hog batches/s:

| hogs | default | `preempt` | `fair` |
| --- | --- | --- | --- |
| 1 | 1,086 | 299 | 235 |
| 2 | 1,501 | 296 | 451 |
| 4 | 2,615 | 293 | 1,142 |
| 8 | 4,745 | 600 | 1,475 |

`preempt` holds the tail in the tens of microseconds at every contention level
and pays for it in compute, keeping between 11 % and 28 % of the default
scheduler's throughput. Weighted virtual time reaches the same tail up to four
hogs, and at two and four hogs keeps 1.5 to 3.9 times more compute than
`preempt`; at one hog the two are comparable. At eight hogs its tail degrades to
954 µs. Neither policy holds both properties across the whole range. Making the
switch cheap enough that prioritisation costs little is what Aeolia's user
interrupts buy.

The throughput column is the noisiest measurement here and varies by a factor of
two between runs on an otherwise busy laptop; the latency columns are stable.

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

`run_all.sh` runs T1, T1b, T2, T2c, T3, T3b and T5, then plots them. It skips
the optional SPDK step if SPDK is not built, and it leaves out `run_o*.sh`,
because `run_o2.sh` recreates the device at different latencies and would
disturb the other runs. Run those separately afterwards.

> **Careful:** the filesystem tests run `mkfs` on the device they are given and
> mount it at `/mnt/mfs_ext4`, which destroys whatever was on it. They refuse
> any device that is not a `null_blk` device or that is currently mounted; set
> `ALLOW_ANY_DEV=1` only if you mean it.

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
            lib.sh        device guard and scheduler attach/detach, sourced
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
| `run_t3.sh` | `mfs` vs `ext4`, default cache | `t3_fs_micro.csv` |
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
- `null_blk` ignores `completion_nsec` on the poll-queue path, so `iou_poll` and
  the `poll` rows of T1b and T5 are a floor for the polling stack rather than a
  comparison at the same device latency. The interrupt paths are unaffected, so
  the `iou` to `iou_active` gap this testbed is built around still holds.
