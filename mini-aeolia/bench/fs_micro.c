// fs_micro.c — Test T3: filesystem micro-benchmarks.
//
// Runs the same create / write / read / stat workload against either:
//   --target mfs    the mini library filesystem on a raw block device (--dev)
//   --target posix  a kernel filesystem (e.g. ext4) under a directory (--dir)
//
// Reports per-op latency and ops/sec per phase. Without its block cache
// (MFS_CACHE_BLOCKS=0) mfs is expected to TRAIL cached ext4, because every op
// becomes a synchronous device round trip; quantifying that gap is the point.
//
//   sudo ./fs_micro --target mfs   --dev /dev/nullb0 --nfiles 5000 --fsize 4096 --csv mfs
//   sudo ./fs_micro --target posix --dir /mnt/x      --nfiles 5000 --fsize 4096 --csv ext4

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include "../libfs/mfs.h"

static inline uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec; }

static void report(const char *target, const char *op, long n, uint64_t ns, int csv) {
    double us = (double)ns / 1000.0 / n;
    double ops = 1e9 * n / ns;
    if (csv) printf("%s,%s,%ld,%.3f,%.0f\n", target, op, n, us, ops);
    else printf("  %-7s %-7s n=%ld  %.2f us/op  %.0f ops/s\n", target, op, n, us, ops);
}

int main(int argc, char **argv) {
    const char *target = "mfs", *dev = "/dev/nullb0", *dir = "/tmp/fsmicro";
    long nfiles = 5000; size_t fsize = 4096; int csv = 0; const char *label = NULL;

    static struct option o[] = {
        {"target",required_argument,0,'t'},{"dev",required_argument,0,'d'},
        {"dir",required_argument,0,'D'},{"nfiles",required_argument,0,'n'},
        {"fsize",required_argument,0,'f'},{"csv",required_argument,0,'C'},{0,0,0,0}};
    int c;
    while ((c=getopt_long(argc,argv,"t:d:D:n:f:C:",o,NULL))!=-1) switch(c){
        case 't': target=optarg; break; case 'd': dev=optarg; break;
        case 'D': dir=optarg; break;   case 'n': nfiles=strtol(optarg,0,0); break;
        case 'f': fsize=strtoul(optarg,0,0); break;
        case 'C': label=optarg; csv=1; break; default: return 2; }
    if (!label) label = target;

    void *buf; if (posix_memalign(&buf, 4096, fsize)) return 1;
    memset(buf, 0xAB, fsize);
    void *rbuf; if (posix_memalign(&rbuf, 4096, fsize)) return 1;
    char name[64];
    uint64_t t0;

    if (strcmp(target, "mfs") == 0) {
        if (mfs_format(dev, 0)) { perror("mfs_format"); return 1; }
        mfs_t fs;
        if (mfs_mount(dev, &fs)) { perror("mfs_mount"); return 1; }
        int *inos = malloc(nfiles * sizeof(int));

        t0 = now_ns();
        for (long i=0;i<nfiles;i++){ snprintf(name,sizeof name,"f%ld",i);
            inos[i]=mfs_create(&fs,name); if(inos[i]<0){perror("create");return 1;} }
        report(label,"create",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++) if(mfs_write(&fs,inos[i],buf,fsize,0)<0){perror("write");return 1;}
        report(label,"write",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++) if(mfs_read(&fs,inos[i],rbuf,fsize,0)<0){perror("read");return 1;}
        report(label,"read",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++){ struct mfs_stat st; if(mfs_stat(&fs,inos[i],&st)){perror("stat");return 1;} }
        report(label,"stat",nfiles,now_ns()-t0,csv);

        mfs_umount(&fs); free(inos);
    } else { /* posix */
        mkdir(dir, 0755);
        int *fds = malloc(nfiles * sizeof(int));

        t0 = now_ns();
        for (long i=0;i<nfiles;i++){ snprintf(name,sizeof name,"%s/f%ld",dir,i);
            fds[i]=open(name,O_CREAT|O_RDWR,0644); if(fds[i]<0){perror("create");return 1;} }
        report(label,"create",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++) if(pwrite(fds[i],buf,fsize,0)!=(ssize_t)fsize){perror("write");return 1;}
        report(label,"write",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++) if(pread(fds[i],rbuf,fsize,0)!=(ssize_t)fsize){perror("read");return 1;}
        report(label,"read",nfiles,now_ns()-t0,csv);

        t0 = now_ns();
        for (long i=0;i<nfiles;i++){ struct stat st; if(fstat(fds[i],&st)){perror("stat");return 1;} }
        report(label,"stat",nfiles,now_ns()-t0,csv);

        for (long i=0;i<nfiles;i++) close(fds[i]);
        free(fds);
    }
    free(buf); free(rbuf);
    return 0;
}
