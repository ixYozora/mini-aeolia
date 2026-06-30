// mfs.c — implementation of the mini library filesystem. See mfs.h.
#define _GNU_SOURCE
#include "mfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <liburing.h>

#define INO_PER_BLK (MFS_BS / sizeof(struct mfs_dinode))   /* 32 */
#define DIRENT_PER_BLK (MFS_BS / sizeof(struct mfs_dirent)) /* 64 */
#define PTR_PER_BLK (MFS_BS / sizeof(uint32_t))             /* 1024 */

/* in-memory accelerators not persisted on disk */
static uint8_t *g_imap;        /* inode-used bitmap, rebuilt at mount */
static uint32_t g_ino_rover;   /* next-inode hint */
static uint64_t g_dir_slot;    /* append cursor for root-dir entries */

static inline int bit_get(const uint8_t *m, uint64_t i){ return (m[i>>3]>>(i&7))&1; }
static inline void bit_set(uint8_t *m, uint64_t i){ m[i>>3] |= (uint8_t)(1u<<(i&7)); }
static inline void bit_clr(uint8_t *m, uint64_t i){ m[i>>3] &= (uint8_t)~(1u<<(i&7)); }

/* ---- raw block I/O via io_uring (synchronous depth-1) ---- */
static int bio(mfs_t *fs, int is_write, uint64_t blk, void *buf) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(fs->ring);
    if (is_write) io_uring_prep_write(sqe, fs->fd, buf, MFS_BS, blk * MFS_BS);
    else          io_uring_prep_read (sqe, fs->fd, buf, MFS_BS, blk * MFS_BS);
    if (io_uring_submit_and_wait(fs->ring, 1) < 0) return -1;
    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(fs->ring, &cqe) < 0) return -1;
    int res = cqe->res;
    io_uring_cqe_seen(fs->ring, cqe);
    return (res == (int)MFS_BS) ? 0 : -1;
}
/* ---- write-back block cache (#1): hash + LRU, dirty tracking ----
 * bread/bwrite go through the cache; the raw bio() above is only used on a miss
 * (load) or on dirty eviction/flush. Write-back: bwrite only marks dirty.     */
struct bcache_entry {
    uint64_t blkno;
    int   in_use, dirty;
    int32_t hnext, lprev, lnext;   /* hash chain + LRU links (slot indices) */
};
typedef struct bcache {
    uint32_t cap, mask, used;
    int32_t *htab;                 /* hash heads, size mask+1 (-1 = empty) */
    struct bcache_entry *ent;      /* cap entries */
    uint8_t *data;                 /* cap * MFS_BS, 4096-aligned */
    int32_t  lru_head, lru_tail;   /* MRU = head */
} bcache_t;

static inline uint8_t *edata(bcache_t *c, int32_t i){ return c->data + (size_t)i*MFS_BS; }

static void lru_remove(bcache_t *c, int32_t i){
    struct bcache_entry *e=&c->ent[i];
    if (e->lprev>=0) c->ent[e->lprev].lnext=e->lnext; else c->lru_head=e->lnext;
    if (e->lnext>=0) c->ent[e->lnext].lprev=e->lprev; else c->lru_tail=e->lprev;
    e->lprev=e->lnext=-1;
}
static void lru_push_front(bcache_t *c, int32_t i){
    struct bcache_entry *e=&c->ent[i];
    e->lprev=-1; e->lnext=c->lru_head;
    if (c->lru_head>=0) c->ent[c->lru_head].lprev=i;
    c->lru_head=i;
    if (c->lru_tail<0) c->lru_tail=i;
}
static void hash_insert(bcache_t *c, int32_t i){
    uint32_t h=c->ent[i].blkno & c->mask;
    c->ent[i].hnext=c->htab[h]; c->htab[h]=i;
}
static void hash_remove(bcache_t *c, int32_t i){
    uint32_t h=c->ent[i].blkno & c->mask;
    int32_t *p=&c->htab[h];
    while (*p>=0){ if (*p==i){ *p=c->ent[i].hnext; break; } p=&c->ent[*p].hnext; }
    c->ent[i].hnext=-1;
}

/* return the slot for `blk`, loading from device if `load` and it was a miss */
static struct bcache_entry *cache_slot(mfs_t *fs, uint64_t blk, int load){
    bcache_t *c=fs->cache;
    uint32_t h=blk & c->mask;
    for (int32_t i=c->htab[h]; i>=0; i=c->ent[i].hnext)
        if (c->ent[i].in_use && c->ent[i].blkno==blk){ lru_remove(c,i); lru_push_front(c,i); return &c->ent[i]; }
    int32_t slot;
    if (c->used < c->cap) {
        slot = c->used++;
    } else {
        slot = c->lru_tail;                       /* evict LRU */
        struct bcache_entry *ev=&c->ent[slot];
        if (ev->dirty){ if (bio(fs,1,ev->blkno,edata(c,slot))) return NULL; ev->dirty=0; }
        hash_remove(c, slot);
        lru_remove(c, slot);
    }
    struct bcache_entry *e=&c->ent[slot];
    e->blkno=blk; e->in_use=1; e->dirty=0;
    if (load){ if (bio(fs,0,blk,edata(c,slot))) return NULL; }
    hash_insert(c, slot);
    lru_push_front(c, slot);
    return e;
}

static int bread(mfs_t *fs, uint64_t blk, void *buf){
    bcache_t *c=fs->cache;
    if (!c) return bio(fs,0,blk,buf);
    struct bcache_entry *e=cache_slot(fs, blk, 1);
    if (!e) return -1;
    memcpy(buf, edata(c, (int32_t)(e - c->ent)), MFS_BS);
    return 0;
}
static int bwrite(mfs_t *fs, uint64_t blk, void *buf){
    bcache_t *c=fs->cache;
    if (!c) return bio(fs,1,blk,buf);
    struct bcache_entry *e=cache_slot(fs, blk, 0);   /* full overwrite -> no load */
    if (!e) return -1;
    memcpy(edata(c, (int32_t)(e - c->ent)), buf, MFS_BS);
    e->dirty=1;
    return 0;
}

static bcache_t *cache_new(uint32_t cap){
    if (cap==0) return NULL;
    bcache_t *c=calloc(1,sizeof(*c));
    c->cap=cap;
    uint32_t m=1; while (m<cap) m<<=1; c->mask=m-1;
    c->htab=malloc((size_t)(c->mask+1)*sizeof(int32_t));
    for (uint32_t i=0;i<=c->mask;i++) c->htab[i]=-1;
    c->ent=calloc(cap,sizeof(struct bcache_entry));
    if (posix_memalign((void**)&c->data,4096,(size_t)cap*MFS_BS)){ free(c); return NULL; }
    c->lru_head=c->lru_tail=-1; c->used=0;
    return c;
}
static int cache_flush(mfs_t *fs){
    bcache_t *c=fs->cache; if (!c) return 0;
    for (uint32_t i=0;i<c->used;i++)
        if (c->ent[i].in_use && c->ent[i].dirty){
            if (bio(fs,1,c->ent[i].blkno,edata(c,(int32_t)i))) return -1;
            c->ent[i].dirty=0;
        }
    return 0;
}
static void cache_free(mfs_t *fs){
    bcache_t *c=fs->cache; if (!c) return;
    free(c->htab); free(c->ent); free(c->data); free(c);
    fs->cache=NULL;
}

/* ---- inode table access ---- */
static void ino_loc(mfs_t *fs, int ino, uint64_t *blk, uint32_t *off) {
    *blk = fs->sb.inode_start + ino / INO_PER_BLK;
    *off = (ino % INO_PER_BLK) * sizeof(struct mfs_dinode);
}
static int read_inode(mfs_t *fs, int ino, struct mfs_dinode *di) {
    uint64_t blk; uint32_t off; ino_loc(fs, ino, &blk, &off);
    if (bread(fs, blk, fs->iobuf)) return -1;
    memcpy(di, (char*)fs->iobuf + off, sizeof(*di));
    return 0;
}
static int write_inode(mfs_t *fs, int ino, const struct mfs_dinode *di) {
    uint64_t blk; uint32_t off; ino_loc(fs, ino, &blk, &off);
    if (bread(fs, blk, fs->iobuf)) return -1;          /* read-modify-write */
    memcpy((char*)fs->iobuf + off, di, sizeof(*di));
    return bwrite(fs, blk, fs->iobuf);
}

/* ---- block allocator (persisted bitmap) ---- */
static long balloc(mfs_t *fs) {
    for (uint64_t b = fs->sb.data_start; b < fs->sb.nblocks; b++) {
        if (!bit_get(fs->bitmap, b)) {
            bit_set(fs->bitmap, b);
            /* zero the new block */
            memset(fs->iobuf, 0, MFS_BS);
            if (bwrite(fs, b, fs->iobuf)) return -1;
            return (long)b;
        }
    }
    errno = ENOSPC; return -1;
}

/* logical->physical block of an inode, allocating along the way if `alloc` */
static long bmap(mfs_t *fs, struct mfs_dinode *di, uint32_t lblk, int alloc) {
    if (lblk < MFS_NDIRECT) {
        if (!di->direct[lblk] && alloc) {
            long b = balloc(fs); if (b < 0) return -1;
            di->direct[lblk] = (uint32_t)b; di->blocks++;
        }
        return di->direct[lblk] ? (long)di->direct[lblk] : -1;
    }
    uint32_t idx = lblk - MFS_NDIRECT;
    if (idx >= PTR_PER_BLK) { errno = EFBIG; return -1; }
    if (!di->indirect) {
        if (!alloc) return -1;
        long b = balloc(fs); if (b < 0) return -1;
        di->indirect = (uint32_t)b; di->blocks++;
    }
    /* read indirect block into a private aligned buffer */
    static __thread uint32_t *ind;   /* reused per thread */
    if (!ind && posix_memalign((void**)&ind, 4096, MFS_BS)) return -1;
    if (bread(fs, di->indirect, ind)) return -1;
    if (!ind[idx]) {
        if (!alloc) return -1;
        long b = balloc(fs); if (b < 0) return -1;
        ind[idx] = (uint32_t)b; di->blocks++;
        if (bwrite(fs, di->indirect, ind)) return -1;
    }
    return (long)ind[idx];
}

/* ---- directory (flat root) ---- */
static int dir_add(mfs_t *fs, const char *name, int ino) {
    struct mfs_dinode root;
    if (read_inode(fs, MFS_ROOT_INO, &root)) return -1;
    uint64_t slot = g_dir_slot;
    uint32_t lblk = slot / DIRENT_PER_BLK;
    uint32_t within = slot % DIRENT_PER_BLK;
    long phys = bmap(fs, &root, lblk, 1);
    if (phys < 0) return -1;
    if (bread(fs, phys, fs->iobuf)) return -1;
    struct mfs_dirent *de = (struct mfs_dirent*)((char*)fs->iobuf + within*sizeof(*de));
    de->ino = ino;
    de->namelen = (uint32_t)strlen(name);
    strncpy(de->name, name, MFS_NAMELEN-1);
    de->name[MFS_NAMELEN-1] = 0;
    if (bwrite(fs, phys, fs->iobuf)) return -1;
    g_dir_slot++;
    uint64_t newsize = g_dir_slot * sizeof(struct mfs_dirent);
    if (newsize > root.size) root.size = newsize;
    return write_inode(fs, MFS_ROOT_INO, &root);
}
static int dir_find(mfs_t *fs, const char *name, int remove) {
    struct mfs_dinode root;
    if (read_inode(fs, MFS_ROOT_INO, &root)) return -1;
    uint64_t nslots = root.size / sizeof(struct mfs_dirent);
    for (uint64_t s = 0; s < nslots; s++) {
        uint32_t lblk = s / DIRENT_PER_BLK, within = s % DIRENT_PER_BLK;
        long phys = bmap(fs, &root, lblk, 0);
        if (phys < 0) continue;
        if (bread(fs, phys, fs->iobuf)) return -1;
        struct mfs_dirent *de = (struct mfs_dirent*)((char*)fs->iobuf + within*sizeof(*de));
        if (de->ino && strncmp(de->name, name, MFS_NAMELEN) == 0) {
            int ino = de->ino;
            if (remove) { de->ino = 0; if (bwrite(fs, phys, fs->iobuf)) return -1; }
            return ino;
        }
    }
    errno = ENOENT; return -1;
}

/* ---- inode allocator (in-memory imap + rover) ---- */
static int ialloc(mfs_t *fs, uint32_t mode) {
    for (uint64_t tries = 0; tries < fs->sb.ninodes; tries++) {
        uint32_t i = g_ino_rover;
        g_ino_rover = (g_ino_rover + 1) % fs->sb.ninodes;
        if (i < 2) continue;                 /* 0 reserved, 1 = root */
        if (!bit_get(g_imap, i)) {
            bit_set(g_imap, i);
            struct mfs_dinode di; memset(&di, 0, sizeof(di));
            di.mode = mode; di.nlink = 1;
            if (write_inode(fs, i, &di)) return -1;
            return (int)i;
        }
    }
    errno = ENOSPC; return -1;
}

/* ---- public API ---- */
int mfs_create(mfs_t *fs, const char *name) {
    int ino = ialloc(fs, 1);
    if (ino < 0) return -1;
    if (dir_add(fs, name, ino)) return -1;
    return ino;
}
int mfs_lookup(mfs_t *fs, const char *name){ return dir_find(fs, name, 0); }
int mfs_unlink(mfs_t *fs, const char *name) {
    int ino = dir_find(fs, name, 1);
    if (ino < 0) return -1;
    struct mfs_dinode di;
    if (read_inode(fs, ino, &di)) return -1;
    for (uint32_t i = 0; i < MFS_NDIRECT; i++)
        if (di.direct[i]) bit_clr(fs->bitmap, di.direct[i]);
    if (di.indirect) bit_clr(fs->bitmap, di.indirect);
    memset(&di, 0, sizeof(di));
    bit_clr(g_imap, ino);
    return write_inode(fs, ino, &di);
}
int mfs_stat(mfs_t *fs, int ino, struct mfs_stat *st) {
    struct mfs_dinode di;
    if (read_inode(fs, ino, &di)) return -1;
    st->mode = di.mode; st->nlink = di.nlink; st->size = di.size; st->blocks = di.blocks;
    return 0;
}

ssize_t mfs_write(mfs_t *fs, int ino, const void *buf, size_t n, off_t off) {
    struct mfs_dinode di;
    if (read_inode(fs, ino, &di)) return -1;
    const char *p = buf; size_t done = 0;
    while (done < n) {
        uint64_t pos = off + done;
        uint32_t lblk = pos / MFS_BS, boff = pos % MFS_BS;
        size_t chunk = MFS_BS - boff; if (chunk > n - done) chunk = n - done;
        long phys = bmap(fs, &di, lblk, 1);
        if (phys < 0) return -1;
        if (chunk != MFS_BS) {                 /* partial: read-modify-write */
            if (bread(fs, phys, fs->iobuf)) return -1;
        }
        memcpy((char*)fs->iobuf + boff, p + done, chunk);
        if (bwrite(fs, phys, fs->iobuf)) return -1;
        done += chunk;
    }
    if ((uint64_t)off + n > di.size) di.size = off + n;
    if (write_inode(fs, ino, &di)) return -1;
    return (ssize_t)done;
}
ssize_t mfs_read(mfs_t *fs, int ino, void *buf, size_t n, off_t off) {
    struct mfs_dinode di;
    if (read_inode(fs, ino, &di)) return -1;
    if ((uint64_t)off >= di.size) return 0;
    if (off + n > di.size) n = di.size - off;
    char *p = buf; size_t done = 0;
    while (done < n) {
        uint64_t pos = off + done;
        uint32_t lblk = pos / MFS_BS, boff = pos % MFS_BS;
        size_t chunk = MFS_BS - boff; if (chunk > n - done) chunk = n - done;
        long phys = bmap(fs, &di, lblk, 0);
        if (phys < 0) { memset(p + done, 0, chunk); }   /* hole */
        else { if (bread(fs, phys, fs->iobuf)) return -1;
               memcpy(p + done, (char*)fs->iobuf + boff, chunk); }
        done += chunk;
    }
    return (ssize_t)done;
}

/* ---- mount / format / sync ---- */
static uint64_t dev_bytes(int fd) {
    struct stat st; uint64_t sz = 0;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) return st.st_size;
    ioctl(fd, BLKGETSIZE64, &sz); return sz;
}

int mfs_format(const char *path, uint64_t size_bytes) {
    int fd = open(path, O_RDWR | O_DIRECT);
    if (fd < 0) return -1;
    uint64_t devb = dev_bytes(fd);
    if (size_bytes == 0 || size_bytes > devb) size_bytes = devb;
    uint64_t nblocks = size_bytes / MFS_BS;
    uint64_t ninodes = nblocks / 8; if (ninodes < 64) ninodes = 64;
    uint64_t bm_blocks = ((nblocks + 7) / 8 + MFS_BS - 1) / MFS_BS;
    uint64_t in_blocks = (ninodes * sizeof(struct mfs_dinode) + MFS_BS - 1) / MFS_BS;

    struct mfs_super sb = {0};
    sb.magic = MFS_MAGIC; sb.nblocks = nblocks; sb.ninodes = ninodes;
    sb.bitmap_start = 1;
    sb.inode_start  = sb.bitmap_start + bm_blocks;
    sb.data_start   = sb.inode_start + in_blocks;
    sb.root_ino     = MFS_ROOT_INO;

    void *blk; if (posix_memalign(&blk, 4096, MFS_BS)) { close(fd); return -1; }

    /* superblock */
    memset(blk, 0, MFS_BS); memcpy(blk, &sb, sizeof(sb));
    if (pwrite(fd, blk, MFS_BS, 0) != (ssize_t)MFS_BS) goto err;

    /* zero inode table */
    memset(blk, 0, MFS_BS);
    for (uint64_t b = 0; b < in_blocks; b++)
        if (pwrite(fd, blk, MFS_BS, (sb.inode_start + b) * MFS_BS) != (ssize_t)MFS_BS) goto err;

    /* build + write bitmap: mark metadata blocks [0, data_start) and the root
       dir's first data block (data_start) as used */
    uint64_t bm_bytes = bm_blocks * MFS_BS;
    uint8_t *bm = calloc(1, bm_bytes); if (!bm) goto err;
    for (uint64_t b = 0; b <= sb.data_start; b++) bit_set(bm, b);  /* +1 root data blk */
    for (uint64_t b = 0; b < bm_blocks; b++)
        if (pwrite(fd, bm + b*MFS_BS, MFS_BS, (sb.bitmap_start + b)*MFS_BS) != (ssize_t)MFS_BS)
            { free(bm); goto err; }
    free(bm);

    /* root inode (dir) with one data block at data_start */
    struct mfs_dinode root = {0};
    root.mode = 2; root.nlink = 2; root.size = 0; root.blocks = 1;
    root.direct[0] = (uint32_t)sb.data_start;
    {   uint64_t iblk; uint32_t ioff;
        iblk = sb.inode_start + MFS_ROOT_INO / INO_PER_BLK;
        ioff = (MFS_ROOT_INO % INO_PER_BLK) * sizeof(struct mfs_dinode);
        memset(blk, 0, MFS_BS);
        if (pread(fd, blk, MFS_BS, iblk*MFS_BS) != (ssize_t)MFS_BS) goto err;
        memcpy((char*)blk + ioff, &root, sizeof(root));
        if (pwrite(fd, blk, MFS_BS, iblk*MFS_BS) != (ssize_t)MFS_BS) goto err;
    }
    /* zero root data block */
    memset(blk, 0, MFS_BS);
    if (pwrite(fd, blk, MFS_BS, sb.data_start * MFS_BS) != (ssize_t)MFS_BS) goto err;

    free(blk); fsync(fd); close(fd);
    return 0;
err:
    free(blk); close(fd); return -1;
}

int mfs_mount(const char *path, mfs_t *fs) {
    memset(fs, 0, sizeof(*fs));
    fs->fd = open(path, O_RDWR | O_DIRECT);
    if (fs->fd < 0) return -1;
    fs->ring = calloc(1, sizeof(struct io_uring));
    if (io_uring_queue_init(8, fs->ring, 0) < 0) return -1;
    if (posix_memalign(&fs->iobuf, 4096, MFS_BS)) return -1;

    /* block cache: MFS_CACHE_BLOCKS (default 8192 = 32 MB); 0 disables it */
    { const char *cb=getenv("MFS_CACHE_BLOCKS");
      uint32_t cap = cb ? (uint32_t)strtoul(cb,NULL,0) : 8192u;
      fs->cache = cache_new(cap); }

    if (bread(fs, 0, fs->iobuf)) return -1;
    memcpy(&fs->sb, fs->iobuf, sizeof(fs->sb));
    if (fs->sb.magic != MFS_MAGIC) { errno = EINVAL; return -1; }

    /* load block bitmap into memory */
    uint64_t bm_blocks = ((fs->sb.nblocks + 7)/8 + MFS_BS - 1) / MFS_BS;
    fs->bitmap_bytes = bm_blocks * MFS_BS;
    if (posix_memalign((void**)&fs->bitmap, 4096, fs->bitmap_bytes)) return -1;
    for (uint64_t b = 0; b < bm_blocks; b++)
        if (bread(fs, fs->sb.bitmap_start + b, fs->bitmap + b*MFS_BS)) return -1;

    /* rebuild in-memory inode-used bitmap + dir append cursor */
    g_imap = calloc(1, (fs->sb.ninodes + 7)/8);
    for (uint64_t i = 0; i < fs->sb.ninodes; i++) {
        struct mfs_dinode di;
        if (read_inode(fs, (int)i, &di)) return -1;
        if (di.mode) bit_set(g_imap, i);
    }
    g_ino_rover = 2;
    struct mfs_dinode root; read_inode(fs, MFS_ROOT_INO, &root);
    g_dir_slot = root.size / sizeof(struct mfs_dirent);
    return 0;
}

int mfs_sync(mfs_t *fs) {
    uint64_t bm_blocks = fs->bitmap_bytes / MFS_BS;
    for (uint64_t b = 0; b < bm_blocks; b++)
        if (bwrite(fs, fs->sb.bitmap_start + b, fs->bitmap + b*MFS_BS)) return -1;
    if (cache_flush(fs)) return -1;          /* push dirty cached blocks to disk */
    fsync(fs->fd);
    return 0;
}
void mfs_umount(mfs_t *fs) {
    mfs_sync(fs);                            /* flushes the cache */
    cache_free(fs);
    if (fs->ring) { io_uring_queue_exit(fs->ring); free(fs->ring); }
    free(fs->bitmap); free(fs->iobuf); free(g_imap); g_imap = NULL;
    close(fs->fd);
}
