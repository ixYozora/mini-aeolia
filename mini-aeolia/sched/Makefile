# Build the miniaeo sched_ext scheduler (BPF object + skeleton + loader).
CLANG    ?= clang
BPFTOOL  ?= bpftool
ARCH     := x86

BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -I.
LOADER_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null || echo -lbpf)
LOADER_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)

.PHONY: all clean
all: miniaeo_loader

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

miniaeo.bpf.o: miniaeo.bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

miniaeo.skel.h: miniaeo.bpf.o
	$(BPFTOOL) gen skeleton $< name miniaeo > $@

miniaeo_loader: loader.c miniaeo.skel.h
	$(CC) -O2 -g -Wall $(LOADER_CFLAGS) -o $@ loader.c $(LOADER_LIBS)

clean:
	rm -f miniaeo.bpf.o miniaeo.skel.h miniaeo_loader
