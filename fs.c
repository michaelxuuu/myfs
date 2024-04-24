#include "fs.h"

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

struct {
    int vd; // file desc pointing to the "virtual disk"
} fs;

// Disk (block) operations
// Write a disk block
static void block_read(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(write(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Read a disk block
static void block_write(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(read(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Zero a block
static void block_zero(int n) {
    char buf[BLOCKSIZE] = {0};
    block_write(n, buf);
}


// Load super block from disk
static void load_super(struct superblock *su) {
    union dblock b;
    block_read(SUBLOCK_NUM, &b);
    *su = b.su;
}

// Bitmap operations
// Allocate a data block
static u32 bitmap_alloc() {
    union dblock b;
    struct superblock su;
    load_super(&su);
    block_read(su.sbitmap, &b);
    for (int i = 0; i < su.nblock_tot; i ++) {
        if (b.bytes[i] == 0xff)
            continue;
        // bytes[i] must has at least one 0 bit
        int off;
        for (; off < 8; off++)
            if (!((b.bytes[i] >> off) & 1))
                break;
        assert(off != 8);
        b.bytes[i] |= 1 << off;
        block_write(su.sbitmap, &b);
        return off + i * 8 + su.sdata;
    }
    return 0;
}
// Free a data block
static int bitmap_free(u32 n) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (n < su.sdata || n >= su.sdata + su.nblock_dat)
        return -1;
    block_read(su.sbitmap, &b);
    // Double free?
    if (!(b.bytes[n/8] & (1 << (n%8))))
        return -1;
    b.bytes[n/8] &= ~(1 << (n%8));
    block_write(su.sbitmap, &b);
    return 0;
}

// inode operations
// Load the inode with the inode number 'n' into memory
static int inode_load(int n, struct dinode *p) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (n >= su.ninodes)
        return -1;
    block_read(su.sinode + n/NINODES_PER_BLOCK, &b);
    *p = b.inodes[n%NINODES_PER_BLOCK];
    return 0;
}

// Update the on-disk inode with the inode number 'n'
static int inode_save(int n, struct dinode *p) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (n >= su.ninodes)
        return -1;
    block_read(su.sinode + n/NINODES_PER_BLOCK, &b);
    b.inodes[n%NINODES_PER_BLOCK] = *p;
    block_write(su.sinode + n/NINODES_PER_BLOCK, &b);
    return 0;
}

// // Free an inode. Need also to free all data blocks linked
// static int inode_free(u32 n) {
//     union dblock b;
//     struct superblock su;
//     load_super(&su);
//     if (n >= su.ninodes || n == 1) // can't free the root directory inode
//         return -1;
//     block_read(su.sinode + n/NINODES_PER_BLOCK, &b);
//     struct dinode *di = &b.inodes[n%NINODES_PER_BLOCK]; // grab a pointer to it
//     di->type = 0; // free the inode itself
//     // free data blocks
//     for (int i = 0; i < NPTRS_CLASS3; i++) {
//         if (!di->ptrs[i])
//             continue;
            
//         if (i < NDIRECT)
//             assert(!bitmap_free(di->ptrs[i]));
//         else {
//             // read the direct block
//             union dblock ib;
//             block_read(di->ptrs[i], &ib);
//             // free data blocks
//             for (int j = 0; j < NPTRS_PER_BLOCK; j++)
//                 if (ib.addrs[i])
//                     assert(!bitmap_free(di->ptrs[j]));
//             // free the indirect block
//             assert(!bitmap_free(di->ptrs[i]));
//         }
//     }
//     // write the inode back to disk
//     block_write(su.sinode + n/NINODES_PER_BLOCK, &b);
//     return 0;
// }

// Sweeb through the inode blocks and return the inum of the free inode if found.
static int inode_alloc(u16 type) {
    struct superblock su;
    if (type > T_DEV)
        return -1;
    load_super(&su);
    for (int i = 0; i < su.nblock_inode; i++) {
        union dblock b;
        block_read(i + su.sinode, &b);
        for (int j = 0; j < NINODES_PER_BLOCK; j++) {
            if (!b.inodes[j].type) {
                struct dinode *p = &b.inodes[j];
                memset(p, 0, sizeof(*p));
                p->type = type;
                p->nlink = 1;
                block_write(i + su.sinode, &b);
                return 0;
            }
        }
    }
    return -1;
}

static int inode_write(u32 n, char buf[], u32 len, u32 off) {
    struct superblock su;
    load_super(&su);
    struct dinode di;
    if (inode_load(n, &di))
        return -1;
    u32 end = off + len;
    if (end > di.size) di.size = end;
    u32 sb = off/BLOCKSIZE;
    u32 eb = end/BLOCKSIZE;
    // check if there's enough disk space left for us to write
    for (int i = sb; i < eb; i++) {
        u32 sz;
        int tmp;
        u32 start;
        u32 *direct;
        u32 *indirect;
        u32 *dindirect;
        union dblock b; // data block
        union dblock ib; // doubly indirect block (if used)
        union dblock dib; // indirect block (if used)

        direct = 0;
        indirect = 0;
        dindirect = 0;

        sz = (i == sb) ? (BLOCKSIZE - off % BLOCKSIZE) : ((BLOCKSIZE < len) ? BLOCKSIZE : len);
        start = (i == sb) ? off % BLOCKSIZE : 0;

        len -= sz;
        buf += sz;

        if (i >= NBLOCKS_TO_DINDIRECT)
            return -1;

        if (i >= NBLOCKS_TO_INDIRECT)
            dindirect = &di.ptrs[(i - NBLOCKS_TO_INDIRECT) / NBLOCKS_BY_DINDRECT];
        if (dindirect && ((i - NBLOCKS_TO_INDIRECT) % NBLOCKS_BY_DINDRECT) == 0)
                block_read(*dindirect, &dib);

        if (dindirect)
            indirect = &dib.ptrs[(i - (tmp = NBLOCKS_TO_INDIRECT)) / NBLOCKS_BY_INDRECT];
        else if (i >= NBLOCKS_TO_DIRECT)
            indirect = &di.ptrs[(i - (tmp = NBLOCKS_TO_DIRECT)) / NBLOCKS_BY_INDRECT];
        if (indirect && ((i - tmp) % NBLOCKS_BY_INDRECT) == 0)
            block_read(*indirect, &ib);

        if (indirect)
            direct = ib.ptrs[(i - NBLOCKS_TO_DIRECT) % NBLOCKS_BY_INDRECT];
        else
            direct = di.ptrs[i];
    
        block_read(*direct, &b);
        memcpy(&b.bytes[start], buf, len);
        block_write(*direct, &b);

        // write the doubly indirect block and indirect blocks back if they're used (regardless of whether they've been modified or not)
        if (dindirect && ((i - NBLOCKS_TO_INDIRECT) % NBLOCKS_BY_DINDRECT) == (NBLOCKS_BY_DINDRECT - 1))
            block_write(*dindirect, &dib);
        if (indirect && ((i - tmp) % NBLOCKS_BY_INDRECT) == (NBLOCKS_BY_INDRECT - 1))
            block_write(*indirect, &ib);
    }

    // write the inode back
    inode_save(n, &di);
    return 0;
}


static int inode_read(u32 n, char b[], u32 l, u32 o) {

}

int main() {

}
