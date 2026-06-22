# mini-aeolia

An **Aeolia-inspired** userspace storage testbed for commodity hardware. Built
for a seminar study of *Aeolia (SOSP '25)* on a laptop that lacks the UINTR and
MPK silicon Aeolia requires. See `../docs/07-minimal-aeolia-tier-b.md` for the
scope and honesty caveats, and `../docs/08-build-plan.md` for the plan.

> **This is not Aeolia.** It substitutes Aeolia's two defining mechanisms
> (user interrupts, MPK protected sharing) with commodity equivalents
> (io_uring interrupt completion, single trust domain). It reproduces the
> paper's central *finding* qualitatively; it does not reproduce its numbers.

## Layout

```
bench/    lat_probe.c        single-task latency probe (T1)
          coexist.c          LC + CPU-hog tail-latency test (T2)   [M2]
          fs_micro.c         mini-libFS vs ext4 micro-bench (T3)   [M3]
sched/    active_check.bpf.c sched_ext active-checking scheduler   [M2]
libfs/    minimal library filesystem on io_uring                  [M3]
scripts/  setup_nullblk.sh   create low-latency null_blk device
          run_t1.sh ...      test drivers -> results/*.csv
plot/     plot_t1.py ...     CSV -> results/*.png
results/  CSVs + figures
```

## Quick start (M1 — Finding #1)

```sh
# 0. one-time install (root)
sudo apt install -y clang llvm libbpf-dev liburing-dev fio \
  cmake meson ninja-build pkg-config python3-matplotlib python3-numpy python3-pandas

# 1. build userspace binaries
make

# 2. create a low-latency device (root) — approximates an Optane-class SSD
sudo scripts/setup_nullblk.sh        # -> /dev/nullb0

# 3. run T1 latency decomposition (root)
sudo scripts/run_t1.sh /dev/nullb0   # -> results/t1_latency.csv

# 4. plot
python3 plot/plot_t1.py              # -> results/t1_*.png
```

To see Finding #1's scheduler effect, re-run step 3 under the active-checking
scheduler (M2) with `LABEL=active_check sudo scripts/run_t1.sh` while the
scheduler from `sched/` is loaded, then re-plot.

## Status

M1 sources written; pending `apt install` + a couple of `sudo` runs to build and
execute. M2–M5 tracked in `../docs/08-build-plan.md`.
