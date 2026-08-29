# Results

Measurements produced by the scripts in `../scripts/`. Every number here comes
from the CSV named beside it, and `sudo ../scripts/run_all.sh` regenerates the
whole set. See the [top-level README](../../README.md) for what the testbed is
and how to run it.

## Testbed

One run on an Intel Core i7-10510U (4 cores, 8 threads), 16 GB RAM, Ubuntu
26.04, Linux 7.0.0, against `null_blk` at a 3 µs completion delay with
`irqmode=2`. No firmware settings were changed, so hyperthreading, turbo and the
power-saving states are all active, unlike the paper's evaluation machine.

**Absolute values move between runs and between machines. What reproduces is the
ordering and the order of magnitude.** The compute-throughput column is the
noisiest measurement here and varies by up to a factor of two on a busy laptop;
the latency columns are stable.

## The cost of an interrupt is scheduling, not delivery

T1, 4 KB read, queue depth 1, median latency in µs (`t1_latency.csv`):

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
polling figure in this directory is therefore a floor for the polling stack, not
a measurement at 3 µs. The interrupt paths are unaffected, so the `iou` to
`iou_active` gap still holds.

## Coordinated scheduling collapses tail latency

T2, one latency-critical thread sharing a core with 3 compute hogs, p99.9
(`t2_coexist.csv`):

| default (EEVDF) | `scx_fifo` (custom scheduler, no LC awareness) | `scx_coord` (LC-aware) |
| --- | --- | --- |
| 3.10 ms | 6.02 ms | 24.3 µs |

The `scx_fifo` control run stays in the millisecond range, so the 127× win comes
from the policy and not from merely swapping the scheduler out.

## A library filesystem beats ext4 on metadata once it has a cache

T3b, 5000 files of 4 KB on the same device, ops/s (`t3b_cache.csv`):

| op | `mfs` (cache) | `ext4` | `f2fs` | `mfs` vs `ext4` |
| --- | --- | --- | --- | --- |
| create | 870,698 | 39,017 | 56,838 | 22× |
| read | 1,364,798 | 565,819 | 827,057 | 2.4× |
| stat | 7,331,335 | 1,546,468 | 1,571,578 | 4.7× |
| write | 195,755 | 272,057 | 266,685 | 0.72× |

Write stays behind because `mfs` zero-fills every newly allocated block. Without
the cache `mfs` loses on every operation, at 13,799 creates/s against ext4's
39,017, because each one becomes a synchronous device round trip.

## Latency and throughput pull against each other

T2c, three scheduling policies across contention (`sched_policies.csv`).
LC p99.9 latency in µs:

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

## Files

| File | Contents | Produced by |
| --- | --- | --- |
| `t1_latency.csv` | latency of the four I/O paths at three block sizes | `run_t1.sh` |
| `t1_median_latency.png`, `t1_interrupt_gap.png` | figures for the above | `plot_t1.py` |
| `t1_depth.csv`, `t1_depth.png` | IOPS and latency vs queue depth (fio) | `run_t1_depth.sh` |
| `t2_coexist.csv`, `t2_tail_latency.png` | tail latency under contention, three schedulers | `run_t2.sh` |
| `sched_policies.csv`, `sched_latency.png`, `sched_throughput.png` | default vs preempt vs fair | `run_sched.sh` |
| `t3_fs_micro.csv`, `t3_fs_micro.png` | `mfs` vs `ext4`, default cache | `run_t3.sh` |
| `t3b_cache.csv`, `t3b_cache.png` | `mfs` cached and uncached vs `ext4` and `f2fs` | `run_t3_cache.sh` |
| `t4_spdk.csv`, `t4_backend.png` | SPDK kernel bypass against a RAM bdev | `run_m4_spdk.sh` |
| `t5_fio_baselines.csv` | fio storage baselines | `run_baselines.sh` |
| `t6_leveldb.csv`, `t6_leveldb.png` | LevelDB `db_bench` on ext4 vs f2fs | `run_leveldb.sh` |
| `o1_spin_sweep.csv`, `o1_spin_sweep.png` | spin budget vs latency and CPU utilisation | `run_o1.sh` |
| `o2_devlat_sweep.csv`, `o2_devlat_sweep.png` | scheduler overhead vs device latency | `run_o2.sh` |
| `o3_contention.csv`, `o3_contention.png`, `o4_throughput_cost.png` | contention sweep | `run_o3.sh` |

`t4_spdk.csv` and `t6_leveldb.csv` come from separate runs made with the
optional SPDK and LevelDB builds present. The `mean_us` column of `t4_spdk.csv`
is bdevperf's average, not a median.
