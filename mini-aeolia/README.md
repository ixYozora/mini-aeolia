# mini-aeolia — components

Build and usage instructions live in the [top-level README](../README.md). This
file describes what each component is and how the pieces fit together.

> **This is not Aeolia.** It substitutes Aeolia's two defining mechanisms (user
> interrupts and MPK-protected sharing) with commodity equivalents (io_uring
> completion, a single trust domain). It reproduces the paper's findings
> qualitatively; it does not reproduce its numbers.

## bench/

`lat_probe.c` measures single-request read latency against a raw block device
across four paths, selected with `--mode`:

| mode | path |
| --- | --- |
| `posix` | `pread` on the raw device |
| `iou` | io_uring, blocking completion (the thread sleeps and is woken) |
| `iou_active` | io_uring, active checking of the completion queue for `--spin` µs before falling back to sleeping |
| `iou_poll` | io_uring with `IORING_SETUP_IOPOLL` (needs `poll_queues` on the device) |

The gap between `iou` and `iou_active` is the scheduler overhead that Aeolia's
first finding is about. Only the waiting strategy differs between the two; the
submission and completion mechanism is identical.

`coexist.c` runs one latency-critical thread issuing dependent reads on the same
core as `--hogs` compute threads, and reports both the LC tail latency and the
hog throughput. Reporting both is what makes the latency/throughput trade-off
visible.

`fs_micro.c` runs create/write/read/stat over a set of small files against
either `mfs` (`--target mfs --dev`) or a mounted kernel filesystem
(`--target posix --dir`).

## libfs/

`mfs` is a small library filesystem on io_uring: superblock, block and inode
bitmaps, a flat inode table and a single directory. It talks to the raw device
from the application's address space, so there is no VFS layer, no syscall per
operation and no kernel locking. This is the property AeoFS gets its metadata
wins from.

`MFS_CACHE_BLOCKS` sets the write-back block cache size in 4 KB blocks, and `0`
disables it. The cache is what turns `mfs` from slower than ext4 into faster
than ext4 on metadata: without it every operation becomes a synchronous device
round trip, while ext4 is served from the kernel page cache.

There is no journal and no crash consistency. `mfs` is a measurement vehicle,
not a usable filesystem.

## sched/

`miniaeo.bpf.c` is an sched_ext scheduler with three policies, chosen by the
loader's argument:

| mode | policy |
| --- | --- |
| `0` | fifo, no LC awareness. The control run: it shows how much of the win comes from the policy rather than from replacing the kernel scheduler |
| `1` | preempt. A task that has just completed I/O preempts a running compute hog. This is the coordinated policy |
| `2` | fair. Weighted virtual time in the spirit of EEVDF, added to test whether a gentler policy can keep throughput |

`loader.c` attaches the scheduler and must stay alive, since a `struct_ops`
scheduler is only active while the process holds the link. Send it `SIGTERM` to
restore the kernel scheduler.

## scripts/ and plot/

`scripts/setup_nullblk.sh` creates the device. It uses `irqmode=2` (hrtimer) on
purpose: completion has to be asynchronous so the submitting task really sleeps
and is woken. With `irqmode=0` the task never blocks and the effect under study
disappears.

The `run_*.sh` drivers write `results/*.csv`, and `plot/plot_*.py` turn those
into `results/*.png`. Neither reads anything outside this directory, so the
figures can be regenerated from the committed CSVs without re-running any
experiment.
