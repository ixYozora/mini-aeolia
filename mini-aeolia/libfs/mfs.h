// mfs.h — mini library filesystem (the "AeoFS analog").
//
// A small POSIX-like filesystem that lives entirely in userspace and reaches
// the block device directly through io_uring and O_DIRECT: no VFS, no syscall
// per operation, no kernel filesystem. It stands in for AeoFS by demonstrating
// the library filesystem model (paper §7) on commodity hardware. It implements
// neither the MPK trusted/untrusted split nor journaling; see ../README.md for
// what is and is not modelled.
//
// Scope: one flat directory (root), regular files, direct and single-indirect
// block maps. Enough to benchmark create, stat, read and write against ext4.
//
// The mount state includes file-scope bookkeeping, so one filesystem may be
// mounted at a time.

#ifndef MFS_H
#define MFS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define MFS_BS        4096u
#define MFS_MAGIC     0x6D696E6961656FULL   /* "miniaeo" */
#define MFS_NDIRECT   12
#define MFS_NAMELEN   56
#define MFS_ROOT_INO  1

struct mfs_super {            /* block 0 */
    uint64_t magic;
    uint64_t nblocks;
    uint64_t ninodes;
    uint64_t bitmap_start;    /* block index of block-usage bitmap */
    uint64_t inode_start;     /* block index of inode table */
    uint64_t data_start;      /* block index of first data block */
    uint64_t root_ino;
};

struct mfs_dinode {           /* 128 bytes on disk */
    uint32_t mode;            /* 0 = free, 1 = file, 2 = dir */
    uint32_t nlink;
    uint64_t size;
    uint32_t blocks;
    uint32_t direct[MFS_NDIRECT];
    uint32_t indirect;        /* single-indirect block of u32 pointers */
    uint32_t _pad[ (128 - (4+4+8+4+MFS_NDIRECT*4+4)) / 4 ];
};

struct mfs_dirent {           /* 64 bytes on disk */
    uint32_t ino;             /* 0 = empty slot */
    uint32_t namelen;
    char     name[MFS_NAMELEN];
};

struct mfs_stat { uint32_t mode; uint32_t nlink; uint64_t size; uint32_t blocks; };

typedef struct mfs {
    int fd;
    struct mfs_super sb;
    uint8_t *bitmap;          /* in-memory copy of the block bitmap */
    size_t   bitmap_bytes;
    void    *iobuf;           /* aligned 1-block bounce buffer */
    struct io_uring *ring;
    void    *cache;           /* write-back block cache (bcache_t*), NULL = off */
} mfs_t;

/* lifecycle */
int  mfs_format(const char *path, uint64_t size_bytes);
int  mfs_mount(const char *path, mfs_t *fs);
int  mfs_sync(mfs_t *fs);
void mfs_umount(mfs_t *fs);

/* namespace (flat, in root dir) */
int  mfs_create(mfs_t *fs, const char *name);   /* -> ino, or <0 */
int  mfs_lookup(mfs_t *fs, const char *name);   /* -> ino, or <0 */
int  mfs_unlink(mfs_t *fs, const char *name);

/* file I/O (by inode number) */
ssize_t mfs_read (mfs_t *fs, int ino, void *buf, size_t n, off_t off);
ssize_t mfs_write(mfs_t *fs, int ino, const void *buf, size_t n, off_t off);
int     mfs_stat (mfs_t *fs, int ino, struct mfs_stat *st);

#endif
