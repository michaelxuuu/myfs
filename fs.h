/**
 * @file fs.h
 * @author 
 * @brief 
 * @version 0.1
 * @date 2024-05-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "types.h"

// Disk layout
//
// reserved (for booting) (s0) | super block (s64) | log blocks (s65) | inode blocks (s95) | bitmap block (s120) | data blocks (s121)

// Fixed disk parameters
#define BLOCKSIZE 512
#define NBLOCKS_TOT 1024 // Disk size 512k
#define NBLOCKS_RES 64   // Reserve 64 blocks (32k) for booting (MBR and bootloader)

// Fixed FS paramters
#define NBLOCKS_LOG 30
#define SUBLOCK_NUM NBLOCKS_RES // super block starts immediately after the reserved blocks
#define NINODES 200
#define FSMAGIC 0xdeadbeef
#define NULLINUM 0
#define ROOTINUM 1 // root directory inode number

struct superblock {
    // Hardcored disk and fs parameters
    u32 ninodes;
    u32 nblock_tot;
    u32 nblock_res;
    u32 nblock_log;
    u32 nblock_dat;
    // Derived fs parameters
    u32 nblock_inode;
    // Start block of each disk section
    u32 slog;
    u32 sinode;
    u32 sbitmap;
    u32 sdata;
    u32 magic;
};

#define NINODES_PER_BLOCK       (BLOCKSIZE/sizeof(struct dinode))
#define NDIRENTS_PER_BLOCK      (BLOCKSIZE/sizeof(struct dirent))
#define NPTRS_PER_BLOCK         (BLOCKSIZE/sizeof(u32))

// #direct, indirect, and doubly-indirect, and total pointers in an inode
#define NDIRECT                 10
#define NINDRECT                2
#define NDINDRECT               1
#define NPTRS                   (NDIRECT+NINDRECT+NDINDRECT)

#define T_REG 1
#define T_DIR 2
#define T_DEV 3
// On-disk inode sturcture
struct dinode {
    u16 type;
    u16 major;
    u16 minor;
    u16 linkcnt;
    u32 size;
    u32 ptrs[NPTRS];
};

// Directory entry sturcture
// Each directory contains an array of directory entries,
// each pointing to an inode representing a file or another directory.
// These directory entries contain the inode number of the file they point to,
// allowing us to access the file content, as well as the associated file name.
// This structure enables file retrieval by path.
#define MAXNAME 14
struct dirent {
    u16 inum;
    char name[MAXNAME];
};

union block {
    struct superblock su;
    u8  bytes[BLOCKSIZE];
    u32 ptrs[NPTRS_PER_BLOCK];
    struct dinode inodes[NINODES_PER_BLOCK];
    struct dirent dirents[NDIRENTS_PER_BLOCK];
};

void fs_init(const char *vhd);
u32 alloc_inode(u16 type);
int free_inode(u32 n);
int read_inode(u32 n, struct dinode *p);
int write_inode(u32 n, struct dinode *p);
u32 inode_read(u32 n, void *buf, u32 sz, u32 off);
u32 inode_write(u32 n, void *buf, u32 sz, u32 off);
