#include "fs.h"

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

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
static int inode_load(int n, struct inode *p) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (n >= su.ninodes)
        return -1;
    block_read(su.sinode + n/NINODES_PERBLOCK, &b);
    p->num = n;
    p->din = b.inodes[n%NINODES_PERBLOCK];
    return 0;
}

// Update the on-disk inode with the inode number 'p->inum' with p->din
static int inode_save(struct inode *p) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (p->num >= su.ninodes)
        return -1;
    block_read(su.sinode + p->num/NINODES_PERBLOCK, &b);
    b.inodes[p->num%NINODES_PERBLOCK] = p->din;
    block_write(su.sinode + p->num/NINODES_PERBLOCK, &b);
    return 0;
}

// Free an inode. Need also to free all data blocks linked
// in-memory inode is passed in, but only 'p->num' is used
// - the freeing is based on the on-disk inode. It must be
// made sure the on-disk inode is update-to-date!
static int inode_free(struct inode *p) {
    union dblock b;
    struct superblock su;
    load_super(&su);
    if (p->num >= su.ninodes || p->num == 1) // can't free the root directory inode
        return -1;
    block_read(su.sinode + p->num/NINODES_PERBLOCK, &b);
    struct dinode *di = &b.inodes[p->num%NINODES_PERBLOCK]; // grab a pointer to it
    di->type = 0; // free the inode itself
    // free data blocks
    for (int i = 0; i < NDIRECT+NINDRECT; i++) {
        if (!di->addrs[i])
            continue;
            
        if (i < NDIRECT)
            assert(!bitmap_free(di->addrs[i]));
        else {
            // read the direct block
            union dblock ib;
            block_read(di->addrs[i], &ib);
            // free data blocks
            for (int j = 0; j < NADDRS_PERBLOCK; j++)
                if (ib.addrs[i])
                    assert(!bitmap_free(di->addrs[j]));
            // free the indirect block
            assert(!bitmap_free(di->addrs[i]));
        }
    }
    // write the inode back to disk
    block_write(su.sinode + p->num/NINODES_PERBLOCK, &b);
    return 0;
}

static int inode_alloc(struct inode *p) {

}

int main() {

}
