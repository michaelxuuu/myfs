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
static void disk_read(int n, void *buf) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(write(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Read a disk block
static void disk_write(int n, void *buf) {
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
    union block b;
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
    union block b;
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
static int read_inode(u32 n, struct dinode *p) {
    union block b;
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    *p = b.inodes[n%NINODES_PER_BLOCK];
    return 0;
}

// Update the on-disk inode with the inode number 'n'
static int write_inode(u32 n, struct dinode *p) {
    union block b;
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    b.inodes[n%NINODES_PER_BLOCK] = *p;
    disk_write(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    return 0;
}

static int get_ilevel(int ptr_idx) {
    if (ptr_idx < NDIRECT)
        return 0;
    else if (ptr_idx < NDIRECT+NINDRECT)
        return 1;
    else
        return 2;
}

static int free_inode_ptr(u32 n, int ilevel) {
    if (!ilevel)
        assert(bitmap_free(n));
    union block b;
    disk_read(n, &b);
    for (int i = 0; i < NPTRS_PER_BLOCK; i++)
        if (b.ptrs[i])
            free_inode_ptr(b.ptrs[i], ilevel--);
    assert(!bitmap_free(n));
    return 0;
}

// Free an inode. Need also to free all data blocks linked
int free_inode(u32 n) {
    union block b;
    struct dinode di;
    if (n >= fs.su.ninodes)
        return -1;
    read_inode(n, &di);
    di.type = 0;
    for (int i = 0; i < NPTRS; i++)
        if (di.ptrs[i])
            free_ptr(di.ptrs[i], get_ilevel(i));
    return 0;
}

// Sweep through the inode blocks and return the inum of the free inode if found.
int alloc_indoe(u16 type) {
    if (type > T_DEV)
        return -1;
    for (int i = 0; i < fs.su.nblock_inode; i++) {
        union block b;
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

int indoe_ptr_write(u32 *ptr, char *buf[], u32 *bytesleft, u32 off, int ilevel, int *is_first_write, u32 *blockstraveled, u32 sblock, u32 eblock) {
    // We have reached a data block
    union block b;

    if (!*bytesleft)
        return 0;

    if (!*ptr && !(*ptr = bitmap_alloc())) {
        *bytesleft = 0;
        return 1;
    }
    
    if (!ilevel && *bytesleft && *blockstraveled >= sblock && *blockstraveled <= eblock) {
        int start = 0;
        int wsize = bytesleft < BLOCKSIZE ? bytesleft : BLOCKSIZE;
        if (*is_first_write) {
            start = off % BLOCKSIZE;
            wsize = BLOCKSIZE - start;
            *is_first_write = 0;
        }
        buf += wsize;
        bytesleft -= wsize;
        disk_read(*ptr, &b);
        memcpy(&b.bytes[start], buf, wsize);
        disk_write(*ptr, &b);
        (*blockstraveled)++;
        return 0;
    }

    // If it's not a data block, then it must be an indirect block.
    // Caring not whether it is singly-indirect or doubly-indirect,
    // we simply read it and call indoe_ptr_write() with ilevel--
    // and let the next call decide what to do next.
    int r = 0;
    disk_read(*ptr, &b);
    for (int i = 0; i < NPTRS_PER_BLOCK; i++)
        if (b.ptrs[i]) // !!!!!!!
            r |= indoe_ptr_write(b.ptrs[i], buf, bytesleft, off, ilevel--, is_first_write, blockstraveled, sblock, eblock);
    disk_write(*ptr, &b);
    return r;
}

int inode_write(u32 n, char buf[], u32 len, u32 off) {
    struct dinode di;
    u32 end = off + len;
    if (n >= fs.su.ninodes)
        return -1;
    if (read_inode(n, &di))
        return -1;
    u32 sblock = off/BLOCKSIZE;
    u32 eblock = end/BLOCKSIZE;
    for (int i = 0; i < NPTRS; i++) {
        if (i < NDIRECT) {

        } else if (i < NDIRECT+NINDRECT) {

        } else {

        }
    }
    return 0;
}


int inode_read(u32 n, char b[], u32 l, u32 o) {

}

int main() {

}
