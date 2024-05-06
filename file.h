#include "types.h"

// opened file structure
struct ofile {
    u32 off;
    u32 inum;
    u32 refcnt;
    u32 mode;
};

// file status
struct filestat {
    u16 type;
    u32 size;
    u16 linkcnt;
};

int myfs_mknod(char *path, u16 type);
int myfs_open(char *path, u16 mode);
int myfs_seek(int fd, u32 off);
int myfs_write(int fd, void *buf, int sz);
int myfs_read(int fd, void *buf, int sz);
int myfs_unlink(char *path);
int myfs_link(char *new, char *old);
int myfs_stat(int fd, struct filestat *st);
int myfs_close(int fd);
