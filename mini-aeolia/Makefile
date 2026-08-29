# mini-aeolia — the userspace benchmark binaries.
# The sched_ext scheduler has its own build:  make -C sched

CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
URING_CFLAGS := $(shell pkg-config --cflags liburing 2>/dev/null)
URING_LIBS   := $(shell pkg-config --libs liburing 2>/dev/null)

BINDIR := bin
BINS   := $(BINDIR)/lat_probe $(BINDIR)/coexist $(BINDIR)/fs_micro

.PHONY: all clean check-deps
all: check-deps $(BINS)

check-deps:
	@pkg-config --exists liburing || { \
	  echo "ERROR: liburing not found. Run:"; \
	  echo "  sudo apt install -y liburing-dev"; exit 1; }

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BINDIR)/lat_probe: bench/lat_probe.c | $(BINDIR)
	$(CC) $(CFLAGS) $(URING_CFLAGS) -o $@ $< $(URING_LIBS)

$(BINDIR)/coexist: bench/coexist.c | $(BINDIR)
	$(CC) $(CFLAGS) $(URING_CFLAGS) -o $@ $< $(URING_LIBS) -lpthread

$(BINDIR)/fs_micro: bench/fs_micro.c libfs/mfs.c | $(BINDIR)
	$(CC) $(CFLAGS) $(URING_CFLAGS) -o $@ bench/fs_micro.c libfs/mfs.c $(URING_LIBS)

clean:
	rm -rf $(BINDIR)
