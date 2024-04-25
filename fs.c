#include "fs.h"

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

struct {
    int vd; // file desc pointing to the "virtual disk"
    struct superblock su; // in-memory of the on-disk super block
} fs;

// Disk (block) operations
// Write a disk block
static void disk_read(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(write(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Read a disk block
static void disk_write(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(read(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Zero a block
static void zero_block(int n) {
    char buf[BLOCKSIZE] = {0};
    disk_write(n, buf);
}

// Bitmap operations
// Allocate a data block
static u32 bitmap_alloc() {
    union dblock b;
    disk_read(fs.su.sbitmap, &b);
    for (int i = 0; i < fs.su.nblock_tot; i ++) {
        if (b.bytes[i] == 0xff)
            continue;
        // bytes[i] must has at least one 0 bit
        int off;
        for (; off < 8; off++)
            if (!((b.bytes[i] >> off) & 1))
                break;
        assert(off != 8);
        b.bytes[i] |= 1 << off;
        disk_write(fs.su.sbitmap, &b);
        return off + i * 8 + fs.su.sdata;
    }
    return 0;
}
// Free a data block
static int bitmap_free(u32 n) {
    union dblock b;
    if (n < fs.su.sdata || n >= fs.su.sdata + fs.su.nblock_dat)
        return -1;
    disk_read(fs.su.sbitmap, &b);
    // Double free?
    if (!(b.bytes[n/8] & (1 << (n%8))))
        return -1;
    b.bytes[n/8] &= ~(1 << (n%8));
    disk_write(fs.su.sbitmap, &b);
    return 0;
}

// inode operations
// Load the inode with the inode number 'n' into memory
static int read_inode(int n, struct dinode *p) {
    union dblock b;
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    *p = b.inodes[n%NINODES_PER_BLOCK];
    return 0;
}

// Update the on-disk inode with the inode number 'n'
static int write_inode(int n, struct dinode *p) {
    union dblock b;
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    b.inodes[n%NINODES_PER_BLOCK] = *p;
    disk_write(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    return 0;
}

// // Free an inode. Need also to free all data blocks linked
// static int inode_free(u32 n) {
//     union dblock b;
//     struct superblock su;
//     read_super(&su);
//     if (n >= fs.su.ninodes || n == 1) // can't free the root directory inode
//         return -1;
//     disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
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
//             disk_read(di->ptrs[i], &ib);
//             // free data blocks
//             for (int j = 0; j < NPTRS_PER_BLOCK; j++)
//                 if (ib.addrs[i])
//                     assert(!bitmap_free(di->ptrs[j]));
//             // free the indirect block
//             assert(!bitmap_free(di->ptrs[i]));
//         }
//     }
//     // write the inode back to disk
//     disk_write(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
//     return 0;
// }

// Sweeb through the inode blocks and return the inum of the free inode if found.
static int alloc_indoe(u16 type) {
    if (type > T_DEV)
        return -1;
    for (int i = 0; i < fs.su.nblock_inode; i++) {
        union dblock b;
        disk_read(i + fs.su.sinode, &b);
        for (int j = 0; j < NINODES_PER_BLOCK; j++) {
            if (!b.inodes[j].type) {
                struct dinode *p = &b.inodes[j];
                memset(p, 0, sizeof(*p));
                p->type = type;
                p->nlink = 1;
                disk_write(i + fs.su.sinode, &b);
                return 0;
            }
        }
    }
    return -1;
}

static int inode_write(u32 n, char buf[], u32 len, u32 off) {
    struct dinode di;
    u32 sb = off/BLOCKSIZE;
    u32 eb = (off + len)/BLOCKSIZE;
    if (n >= fs.su.ninodes)
        return -1;
    if (read_inode(n, &di))
        return -1;
    // Update the file size if necessary
    if (off + len > di.size)
        di.size = off + len;
    for (int i = sb; i < eb; i++) {
        // In each iteration, we do the following things:
        // 1. Read a block into buffer `d`
        // 2. Write `sz` bytes to it starting from `start`
        // 3. Write it back to disk
        // 4. Update any on-disk data structure we might have changed,
        // (a) including inodes, (b) indirect blocks, and (c) doubly-
        // indirect blocks

        union dblock b;     // Data block
        union dblock ib;    // Doubly indirect block (if used)
        union dblock dib;   // Indirect block (if used)

        u32 sz;             // How much we write to `b`
        int tmp;
        u32 start;          // Where we start writing within `b`

        // A direct pointer can be found in (1) an inode and (2) a singly-indirect block
        u32 *direct;        // &inode->ptr[x] where x falls into the range of indices of direct pointers
                            // *or* points to &ib.ptrs[x]
        // A indirect pointer can be found in (1) an inode and (2) a doubly-indirect block
        u32 *indirect;      // &inode->ptr[x] where x falls into the range of indices of singly-indirect pointers
                            // *or* points to &dib.ptrs[x]
        u32 *dindirect;     // &inode->ptr[x] where x falls into the range of indices of doubly-indirect pointers

        direct = 0;
        indirect = 0;
        dindirect = 0;

        start = 0;
        sz = (BLOCKSIZE < len) ? BLOCKSIZE : len;

        // The start position within the first block we write and the
        // number of bytes we write there depends on `off`
        if (i == sb) {
            start = off % BLOCKSIZE;
            sz = BLOCKSIZE - start;
        }

        // `len` is the number of bytes left to be written
        // buf points to the start of the data hasn't be read from the 
        // input buffer
        len -= sz;
        buf += sz;

        // `i`th block is out-of-bounds
        if (i >= NBLOCKS_TO_DINDIRECT)
            return -1;

        //  `i`th block falls into the range of blocks pointed to by doubly-indirect pointers :o
        if (i >= NBLOCKS_TO_INDIRECT)
            dindirect = &di.ptrs[(i - NBLOCKS_TO_INDIRECT) / NBLOCKS_BY_DINDRECT];
        // Load this doubly-indirect block if `i`th block is the *FIRST* block it has
        if (dindirect && ((i - NBLOCKS_TO_INDIRECT) % NBLOCKS_BY_DINDRECT) == 0)
                disk_read(*dindirect, &dib);

        // If `i`th block is pointed to by a doubly-indirect block, the indirect pointer should be found in the corresponding doubly-indirect block - `dib`
        if (dindirect)
            indirect = &dib.ptrs[(i - (tmp = NBLOCKS_TO_INDIRECT)) / NBLOCKS_BY_INDRECT];
        // If `i`th block is pointed to by a indirect block, the indirect pointer should be found in the inode - `di`
        else if (i >= NBLOCKS_TO_DIRECT)
            indirect = &di.ptrs[(i - (tmp = NBLOCKS_TO_DIRECT)) / NBLOCKS_BY_INDRECT];
        // Load this indirect block if `i`th block is the *FIRST* block it has
        if (indirect && ((i - tmp) % NBLOCKS_BY_INDRECT) == 0)
            disk_read(*indirect, &ib);

        // If `i`th block is pointed to by a indirect block, the direct pointer should be found in the indirect block - `id`
        // (Recall that indirect block could've been loaded from either a doubly-indirect block or indoe)
        if (indirect)
            direct = ib.ptrs[(i - NBLOCKS_TO_DIRECT) % NBLOCKS_BY_INDRECT];
        // If `i`th block is pointed to by a direct pointer, the direct pointer should be found in the inode - `di`
        else
            direct = di.ptrs[i];

        // Writing... :)
        disk_read(*direct, &b);
        memcpy(&b.bytes[start], buf, len);
        disk_write(*direct, &b);

        // Write this doubly-indirect block back to disk if `i`th block is the *last* block it has (regardless of whether it's been modified or not)
        if (dindirect && ((i - NBLOCKS_TO_INDIRECT) % NBLOCKS_BY_DINDRECT) == (NBLOCKS_BY_DINDRECT - 1))
            disk_write(*dindirect, &dib);
        // Write this indirect block back to disk if `i`th block is the *last* block it has (regardless of whether it's been modified or not)
        if (indirect && ((i - tmp) % NBLOCKS_BY_INDRECT) == (NBLOCKS_BY_INDRECT - 1))
            disk_write(*indirect, &ib);
    }

    // write the inode back in case we've modified it which we mostly likely did :)
    write_inode(n, &di);
    return 0;
}


static int inode_read(u32 n, char b[], u32 l, u32 o) {

}

int main() {

}
