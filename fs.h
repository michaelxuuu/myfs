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

#define NDIRECT 12
#define NINDRECT 1
// On-disk inode sturcture
struct dinode {
  u16 type;
  u16 major;
  u16 minor;
  u16 nlink;
  u32 size;
  u32 addrs[NDIRECT+NINDRECT];
};

// in-memory copy of the on-disk inode
struct inode {
    int num;
    struct dinode din;
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

#define NINODES_PERBLOCK (BLOCKSIZE / sizeof(struct dinode))
#define NDIRENTS_PERBLOCK (BLOCKSIZE / sizeof(struct dirent))
#define NADDRS_PERBLOCK (BLOCKSIZE / sizeof(u32))

union dblock {
    struct superblock su;
    u8  bytes[BLOCKSIZE];
    u32 addrs[NADDRS_PERBLOCK];
    struct dinode inodes[NINODES_PERBLOCK];
    struct dirent dirents[NDIRENTS_PERBLOCK];
};

