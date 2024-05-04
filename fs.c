/**
 * @file fs.c
 * @author 
 * @brief 
 * @version 0.1
 * @date 2024-05-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "fs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

static void fs_checker();

/**
 * @brief Structure representing the file system
 */
struct FileSystem {
    int vd;                 /**< File descriptor pointing to the "virtual disk" */
    struct superblock su;   /**< In-memory copy of the superblock */
} fs;


/**
 * @brief Write a disk block
 * 
 * @param n     The number of the block where the data is to be written
 * @param buf   A pointer to the buffer containing the data to be written to the disk block
 */
static void disk_write(int n, void *buf)
{
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(write(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}

/**
 * @brief Read a disk block
 * 
 * @param n     The number of the block to be read from
 * @param buf   A pointer to the buffer where the read data will be stored
 */
static void disk_read(int n, void *buf)
{
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(read(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}

/**
 * @brief Bitmap operations: Allocate a data block
 * 
 * @return u32  The number of the allocated data block, or 0 if allocation fails
 */
static u32 bitmap_alloc() 
{
    union block b;
    disk_read(fs.su.sbitmap, &b);
    for (int i = 0; i < fs.su.nblock_dat / 8; i++) {
        if (b.bytes[i] == 0xff)
            continue;
        // bytes[i] must has at least one 0 bit
        int off;
        for (off = 0; off < 8; off++)
            if (!((b.bytes[i] >> off) & 1))
                break;
        if (off + i * 8 >= fs.su.nblock_dat)
            return 0;
        assert(off != 8);
        b.bytes[i] |= 1 << off;
        disk_write(fs.su.sbitmap, &b);
        return off + i * 8 + fs.su.sdata;
    }
    return 0;
}

/**
 * @brief Bitmap operations: Free a data block
 * 
 * @param n The number of the data block to be freed
 * @return int  Returns 0 if the data block is successfully freed, or -1 if an error occurs
 * 
 */
static int bitmap_free(u32 n) 
{
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

/**
 * @brief Reads an inode from the disk into memory.
 * 
 * @param n The inode number of the inode to be read.
 * @param p Pointer to the struct dinode where the read inode will be stored.
 * @return int Returns 0 on success, -1 on failure.
 */
static int read_inode(u32 n, struct dinode *p) 
{
    union block b;
    // Read a inode block to the buffer
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    // Read the target inode
    *p = b.inodes[n%NINODES_PER_BLOCK];
    return 0;
}

/**
 * @brief Update the on-disk inode with the inode number 'n'.
 * 
 * @param n The inode number of the inode to be written.
 * @param p Pointer to the struct dinode containing the inode to be written.
 * @return int Returns 0 on success, -1 on failure.
 */
static int write_inode(u32 n, struct dinode *p) 
{
    union block b;
    disk_read(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    b.inodes[n%NINODES_PER_BLOCK] = *p;
    disk_write(fs.su.sinode + n/NINODES_PER_BLOCK, &b);
    return 0;
}

// Given a index into the 'ptrs' array in an inode,
// this function returns the indirection level of the
// pointer entry - if it's a direct, singly-indirect,
// or a doubly-indirect pointer.
static int get_ilevel(int ptr_idx) 
{
    if (ptr_idx < NDIRECT)
        return 0;
    else if (ptr_idx < NDIRECT+NINDRECT)
        return 1;
    else
        return 2;
}

// There are three types of blocks:
//
// 1. Data blocks
// 2. Singly-indirect blocks
// 3. Doubly-indirect blocks
//
// We consider all these blocks to be *indirect* blocks
// and distinguish them by their "indirect level," ilevel for short.
//
// Below are the ilevels for each type of block:
// Data blocks:             ilevel=0
// Singly-indirect blocks:  ilevel=1
// Doubly-indirect blocks:  ilevel=2

// Frees a general indirect block (can be a data block if ilevel=0). Recursively
// frees all sub-level blocks based on the ilevel value. For example, an initial
// call with ilevel=2 for a doubly-indirect block will recursively free all
// singly-indirect blocks and their respective data blocks.
static int free_indirect(u32 n, int ilevel) 
{
    // ilevel=0 is the base case: it is a data block
    if (!ilevel) {
        assert(bitmap_free(n));
        return 0;
    }
    // Not a data block. Then it must be an indirect block.
    // We treat doubly-indirect and singly-indirect blocks
    // the same since they are all just a block of pointers.
    union block b;
    // Read the indirect block
    disk_read(n, &b);
    // Free the block after reading into memory
    assert(!bitmap_free(n));
    // Recursively free all referenced sub-level blocks
    for (int i = 0; i < NPTRS_PER_BLOCK; i++)
        if (b.ptrs[i])
            free_indirect(b.ptrs[i], ilevel - 1); // Decrement ilevel per recursion
    return 0;
}

// Free an inode. Also need to free all referenced data blocks.
int free_inode(u32 n) 
{
    union block b;
    struct dinode di;
    if (n >= fs.su.ninodes)
        return -1;
    read_inode(n, &di);
    di.type = 0;
    for (int i = 0; i < NPTRS; i++)
        if (di.ptrs[i])
            free_indirect(di.ptrs[i], get_ilevel(i));
    write_inode(n, &di);
    return 0;
}

/**
 * @brief Allocate an inode
 * 
 * @param type The type of the inode to be allocated
 * @return u32 The inode number of the allocated inode, or -1 if allocation fails
 */
u32 alloc_inode(u16 type) 
{
    // Invalid inode type, return error
    if (type > T_DEV)
        return -1;
    // Loop through all blocks for inode
    for (int i = 0; i < fs.su.nblock_inode; i++) {
        union block b;
        // Read current inode block to the buffer
        disk_read(i + fs.su.sinode, &b);
        for (int j = 0; j < NINODES_PER_BLOCK; j++) {
            // Found a unallocated inode
            if (!b.inodes[j].type) {
                struct dinode *p = &b.inodes[j];
                memset(p, 0, sizeof(*p));
                p->type = type;
                p->nlink = 1;
                // Write back updated inode block
                disk_write(i + fs.su.sinode, &b);
                return i * NINODES_PER_BLOCK + j;
            }
        }
    }
    return -1;
}

// This is the last argument of recursive_rw(), and the struct 
// members are shared among all recursive calls simultaneously.
// This reduces the number of arguments to be passed and makes 
// the code more concise and readable. The struct contains elements
// that have only one global copy across all instances of recursive_rw(),
// generated from a single call to inode_rw(), while each recursive_rw() 
// call has their own private copies of the other two arguments.
struct share_arg {
    u32 boff;   // Current block offset within a file
    u32 sblock; // Start block
    u32 eblock; // End block
    // Note: For each block pointed to by pp, we test if the data block it covers
    // or itself if it is a data block (*boff + nblocks linked to it) overlaps with 
    // the [sblock, eblock] interval, and we skip this block if not.
    u32 off;    // Same off in inode_rw()
    char *buf;  // Same buf in inode_rw()
    u32 left;   // Number of bytes left
    int w;      // Recursive write? Recursive read if 0
};


/**
 * @brief Recursively writes data to disk blocks or indirect blocks.
 * 
 * @param pp Pointer to a block pointer (which could be in an inode or an indirect pointer that caller traverses).
 * @param ilevel Recursion level. 0 means we've reached a data block.
 * @param sa Pointer to a struct share_arg containing shared arguments for the write operation.
 * @return int Returns 0 on success, -1 on failure.
 */
static int recursive_rw(
    u32 *pp,    // Pointer to a block pointer (which could be in an inode or an indirect pointer that caller traverses)
    u32 ilevel, // Recursion level. 0 means we've reached a data block.
    struct share_arg *sa
) {
    // Do we skip this indirect block?
    // Compute data *block coverage* of this indirect (or data) block: [sblock, eblock)
    u32 sblock = sa->boff;
    u32 eblock = sa->boff;
    if (ilevel == 0)
        eblock += 1;
    if (ilevel == 1)
        eblock += NPTRS_PER_BLOCK;
    if (ilevel == 2)
        eblock += NPTRS_PER_BLOCK*NPTRS_PER_BLOCK;
    // Do [sblock, eblock) and [sa->sblock, sa->eblock], the *block coverage* of this w/r operation overlap?
    if (!(sblock <= sa->eblock && sa->sblock < eblock)) {
        sa->boff = eblock;
        sa->off += (eblock - sblock) * BLOCKSIZE;
        return 0;
    }
    if (sa->w) {
        // This indirect (or data) block is involved in this w/r,
        // so it should not be null and we should allocate it if null.
        int zero = 0;
        if (!*pp && ilevel)
            zero = 1;
        if (!*pp && !(*pp = bitmap_alloc()))
            return -1; // ran out of free blocks
        if (zero) {
            char zeros[BLOCKSIZE] = {0};
            disk_write(*pp, &zeros);
        }
    } else {
        // Handle reading sparse files
        if (!*pp) {
            u32 sz = (eblock - sblock) * BLOCKSIZE;
            memset(sa->buf, 0, sz);
            sa->buf += sz;
            sa->left -= sz;
            sa->off += sz;
            sa->boff = eblock;
            return 0;
        }
    }
    union block b;
    // It is an indirect block, start recursion.
    if (ilevel) {
        disk_read(*pp, &b);
        for (int i = 0; i < NPTRS_PER_BLOCK; i++)
            if (recursive_rw(&b.ptrs[i], ilevel - 1, sa)) {
                // If a write failed half way, we do *not* roll back, but
                // leave the blocks already written and abort. However,
                // we DO need to update the indirect block that has been
                // modified. That's why we're writing back to disk this
                // indirect block.
                if (sa->w) disk_write(*pp, &b);
                return -1;
            }
        disk_write(*pp, &b);
        return 0;
    }
    // It's a data block.
    u32 start = sa->off % BLOCKSIZE;
    u32 sz = sa->left < (BLOCKSIZE - start) ? sa->left : (BLOCKSIZE - start);
    disk_read(*pp, &b);
    if (sa->w) {
        memcpy(&b.bytes[start], sa->buf, sz);
        disk_write(*pp, &b);
    } else
        memcpy(sa->buf, &b.bytes[start], sz);
    printf(sa->w ? "write block: %d\n" : "read block: %d\n", *pp);
    sa->buf += sz;
    sa->left -= sz;
    sa->off += sz;
    sa->boff = eblock; // Must update boff.
    return 0;
}


/**
 * @brief Writes data to an inode within the file system.
 * 
 * @param n The inode number representing the inode to which data will be written.
 * @param buf A pointer to the buffer containing the data to be written.
 * @param sz The size of the data to be written.
 * @param off The offset where the writing should start.
 * @return int Returns 0 if the write operation is successful, otherwise returns -1.
 */
static u32 inode_rw(u32 n, void *buf, u32 sz, u32 off, int w)
{
    struct dinode di;
    u32 sbyte = off;
    u32 ebyte = off + sz;
    // Invalid inode number
    if (n >= fs.su.ninodes)
        return -1;
    // Read inode structure
    if (read_inode(n, &di))
        return -1;
    if (!w && sbyte >= di.size)
        return 0;
    if (!w && ebyte >= di.size) {
        ebyte = di.size;
        sz = di.size - sbyte;
    }
    u32 sblock = sbyte/BLOCKSIZE;
    u32 eblock = sbyte/BLOCKSIZE;
    struct share_arg *sa = &(struct share_arg){
        .boff = 0,
        .sblock = sblock,
        .eblock = eblock,
        .off = off,
        .buf = buf,
        .left = sz,
        .w = w
    };

    for (int i = 0; i < NPTRS; i++)
        if (recursive_rw(&di.ptrs[i], get_ilevel(i), sa))
            break;
    u32 consumed = sz - sa->left;
    ebyte = off + consumed; // ebyte should remain unchanged if consumed equals sz, 
                            // indicating that the required amount of bytes has been successfully 
                            // consumed from buf (write) or from disk (read)
    di.size = di.size > ebyte ? di.size : ebyte; // update inode size in case it's a write operation
    if (w) {
        assert(!write_inode(n, &di)); // update inode
        fs_checker();
    }
    return consumed;
}

u32 inode_write(u32 n, void *buf, u32 sz, u32 off) {
    return inode_rw(n, buf, sz, off, 1);
}

u32 inode_read(u32 n, void *buf, u32 sz, u32 off) {
    return inode_rw(n, buf, sz, off, 0);
}

static u32 recursive_count(u32 ptr, int ilevel) 
{
    if (!ptr)
        return 0;
    if (!ilevel)
        return 1;
    union block b;
    u32 cnt = 1; // include this indirect block
    disk_read(ptr, &b);
    for (int i = 0; i < NPTRS_PER_BLOCK; i++)
        cnt += recursive_count(b.ptrs[i], ilevel - 1);
    return cnt;
}

// Check fs correctness
static void fs_checker() 
{
    union block b;
    // Count the number of data blocks referenced by inodes
    u32 datacnt1 = 0;
    u32 datacnt2 = 0;
    for (int i = 0; i < fs.su.nblock_inode; i++) {
        disk_read(fs.su.sinode + i, &b);
        for (int j = 0; j < NINODES_PER_BLOCK; j++)
            if (b.inodes[j].type)
                for (int k = 0; k < NPTRS; k++)
                    datacnt1 += recursive_count(b.inodes[j].ptrs[k], get_ilevel(k));
    }
    // Count the number of data blocks marked as used in the bitmap
    disk_read(fs.su.sbitmap, &b);
    for (int i = 0; i < fs.su.nblock_dat / 8; i++)
        for (int off = 0; off < 8; off++)
            if (((b.bytes[i] >> off) & 1) && (off + i * 8) < fs.su.nblock_dat)
                datacnt2 += 1;
    assert(datacnt1 == datacnt2);
}

// Look up 'name' under the directory pointed to by 'inum.'
// Return the inum of 'name' if found and 0 otherwise.
static u32 dir_lookup(u32 inum, const char *name) 
{
    struct dinode di;
    if (inum >= fs.su.ninodes)
        return NULLINUM;
    read_inode(inum, &di);
    // Not a directory
    if (di.type != T_DIR)
        return NULLINUM;
    u32 off = 0;
    for (int i = 0; i < di.size / sizeof(struct dirent); 
        i++, off += sizeof(struct dirent)) {
        struct dirent de;
        inode_read(inum, &de, sizeof(struct dirent), off);
        if (!strcmp(de.name, name))
            return de.inum;
    }
    return NULLINUM;
}

#define MAX_FILE_PATH 512
u32 fs_lookup(const char *path) {
    // Max file path length capped to 512 bytes,
    // *excluding* the terminating 0
    int l = strnlen(path, MAX_FILE_PATH + 1);
    if (l > MAX_FILE_PATH || l < 1)
        return NULLINUM;
    // This function only start finding from root
    // restricting path must be preceeded by '/'
    if (path[0] != '/')
        return NULLINUM;
    // shortest path is "/" - root path
    if (l == 1)
        return ROOTINUM;
    // Parse the path
    char buf[MAX_FILE_PATH + 1] = {0};
    strncpy(buf, path + 1, sizeof buf); // add 1 to path to skip '/'
    l -= 1;                             // - 1 from l from the same reason
    // replace all '/'s with '\0's to break the path into individual sub-strings
    for (int i = 0; buf[i]; i++)
        if (buf[i] == '/')
            buf[i] = NULLINUM;
    u32 inum = ROOTINUM; // start from root
    for (int i = 0; i < l; i += strlen(&buf[i])) {
        char *name = &buf[i];
        if (!(inum = dir_lookup(inum, name)))
            return NULLINUM;
    }
    return inum;
}

/**
 * @brief print superblock information
 * 
 */
static void printsu() {
    printf("superblock:\n"
            "#inodes:%u\n"
            "#blocks(tot):%u\n"
            "#blocks(res):%u\n"
            "#blocks(log):%u\n"
            "#blocks(ino):%u\n"
            "#blocks(dat):%u\n"
            "start(log):%u\n"
            "start(ino):%u\n"
            "start(bmp):%u\n"
            "start(dat):%u\n"
            "magic:%x\n",
            fs.su.ninodes, 
            fs.su.nblock_tot, 
            fs.su.nblock_res,
            fs.su.nblock_log,
            fs.su.nblock_inode, 
            fs.su.nblock_dat,
            fs.su.slog,
            fs.su.sinode,
            fs.su.sbitmap,
            fs.su.sdata,
            fs.su.magic
    );
}

void fs_init(const char *vhd) {
    if ((fs.vd = open(vhd, O_RDWR, 0644)) == -1) {
        perror("open");
        exit(1);
    }
    // Read super block
    union block b;
    disk_read(SUBLOCK_NUM, &b);
    // fs already installed
    if (b.su.magic == FSMAGIC) {
        // Save a copy of on-disk super block in memory
        fs.su = b.su;
        printsu();
        return;
    }
    // Format vhd
    // Zero the entire disk
    char buf[BLOCKSIZE] = {0};
    for (int i = 0; i < NBLOCKS_TOT; i++)
        disk_write(i, buf);
    // Prep super block
    b.su.ninodes = NINODES;
    b.su.nblock_tot = NBLOCKS_TOT;
    b.su.nblock_res = NBLOCKS_RES;
    b.su.nblock_log = NBLOCKS_LOG;
    b.su.nblock_inode = NINODES / NINODES_PER_BLOCK;
    b.su.nblock_dat = NBLOCKS_TOT - (NBLOCKS_RES + NBLOCKS_LOG + b.su.nblock_inode + 1 + 1);
    b.su.slog = NBLOCKS_RES + 1; // 65
    b.su.sinode = b.su.slog + NBLOCKS_LOG; // 65 + 30
    b.su.sbitmap = b.su.sinode + b.su.nblock_inode;
    b.su.sdata = b.su.sbitmap + 1;
    b.su.magic = FSMAGIC;
    // Write super block to disk
    disk_write(SUBLOCK_NUM, &b);
    // Save a copy in memory
    fs.su = b.su;
    printsu();
    // Reserve inode 0 and 1
    alloc_inode(T_DIR);
    alloc_inode(T_DIR);
}
