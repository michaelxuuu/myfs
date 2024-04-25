// Defines the inode super block and the indoe structure
#include "disk.h"
#include "types.h"

struct superblock {
    // Hardcored (see disk.h)
    u32 ninodes;
    u32 nblock_tot;
    u32 nblock_res;
    u32 nblock_log;
    u32 nblock_dat;
    // Derived
    u32 nblock_inode;
    // Start block of each disk section
    u32 slog;
    u32 sinode;
    u32 sbitmap;
    u32 sdata;
};

#define NINODES_PER_BLOCK       (BLOCKSIZE/sizeof(struct dinode))
#define NDIRENTS_PER_BLOCK      (BLOCKSIZE/sizeof(struct dirent))
#define NPTRS_PER_BLOCK         (BLOCKSIZE/sizeof(u32))

// #direct, indirect, and doubly-indirect, and total pointers in an inode
#define NDIRECT                 10
#define NINDRECT                2
#define NDINDRECT               1
#define NPTRS                   (NDIRECT+NINDRECT+NDINDRECT)

// #blocks pointed to by an inode indirect pointer
#define NBLOCKS_BY_INDRECT      NPTRS_PER_BLOCK
// #blocks pointed to by an inode doubly-indirect pointer
#define NBLOCKS_BY_DINDRECT     (NPTRS_PER_BLOCK*NPTRS_PER_BLOCK)

// #blocks pointed to by all direct pointers in an inode
#define NBLOCKS_TO_DIRECT       NDIRECT
// #blocks pointed to by all direct and indirect pointers in an inode
#define NBLOCKS_TO_INDIRECT     (NBLOCKS_TO_DIRECT+NINDRECT*NPTRS_PER_BLOCK)
// #blocks pointed to by all direct, indirect and doubly-indirect pointers in an indoe
#define NBLOCKS_TO_DINDIRECT    (NBLOCKS_TO_INDIRECT+NDINDRECT*NPTRS_PER_BLOCK*NPTRS_PER_BLOCK)

#define T_REG 1
#define T_DIR 2
#define T_DEV 3
// On-disk inode sturcture
struct dinode {
  u16 type;
  u16 major;
  u16 minor;
  u16 nlink;
  u32 size;
  u32 ptrs[NDIRECT+NINDRECT];
};

// Directory entry sturcture
// Each directory contains an array of directory entries,
// each pointing to an inode representing a file or another directory.
// These directory entries contain the inode number of the file they point to,
// allowing us to access the file content, as well as the associated file name.
// This structure enables file retrieval by path.
#define MAX_FILE_NAME 14
struct dirent {
    u16 inum;
    char name[MAX_FILE_NAME];
};

union block {
    struct superblock su;
    u8  bytes[BLOCKSIZE];
    u32 ptrs[NPTRS_PER_BLOCK];
    struct dinode inodes[NINODES_PER_BLOCK];
    struct dirent dirents[NDIRENTS_PER_BLOCK];
};

