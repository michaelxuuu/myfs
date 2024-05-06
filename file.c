// File abstraction level
// Here users see files and diretories instead of raw inodes

#include "fs.h"
#include "file.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define MAXPATH 64
#define NFILES 100

// opened file table
static struct ofile opened[NFILES];

// Look up 'name' under the directory pointed to by 'inum.'
// Return the *index* of the dirent containing 'name' if found and -1 otherwise.
// Write the offset of the dirent found into *poff if it's not NULL.
static u32 dir_lookup(u32 inum, char *name, u32 *poff)
{
    struct dinode di;
    read_inode(inum, &di);
    // Not a directory
    if (di.type != T_DIR)
        return NULLINUM;
    u32 off = 0;
    for (int i = 0; i < di.size / sizeof(struct dirent); 
        i++, off += sizeof(struct dirent)) {
        struct dirent de;
        inode_read(inum, &de, sizeof(struct dirent), off);
        if (!strcmp(name, de.name)) {
            if (poff)
                *poff = off;
            return de.inum;
        }
    }
    return NULLINUM;
}

// Find the inode corresponds to the given path.
// If parent = 1, stop one level early at the parent dir.
u32 lookup(char *path, int parent) {
    if (!path)
        return NULLINUM;
    int l = strnlen(path, MAXPATH);
    if (l > MAXPATH - 1)
        return NULLINUM;
    // This function only start finding from root
    // restricting path must be preceeded by '/'
    if (path[0] != '/')
        return NULLINUM;
    u32 inum = ROOTINUM; // start from root
    path += 1; // skip the leading slash
    for (;;) {
        if (!*path)
            break;
        // copy the next name from path
        char name[MAXNAME];
        for (l = 0; *path && *path != '/'; l++, path++) {
            if (l >= MAXNAME)
                return NULLINUM;
            name[l] = *path;
        }
        // null-term the name
        name[l] = 0;
        // skip following slashes
        for (; *path && *path == '/'; path++);
        // stop one level early to return the inode of the parent dir
        if (!*path && parent)
            break;
        if (!(inum = dir_lookup(inum, name, 0)))
            return NULLINUM;
    }
    return inum;
}

// Each path can be seen as parent/name
// * Copy the "name" part into the 'name' buf
// * Optionally copy the "parent" part into the 'parent' buf
// * Return a pointer to the start of the "name" part on success
// * Return 0 when no specified, as in path "////////" or ""
char *getname(char *path, char *name, char *parent) {
    if (!path)
        return 0;
    int len = strlen(path);
    if (!len)
        return 0;
    char *p = path+len-1;
    for (; p!=path && p[0]=='/'; p--);
    if (p==path && len!=1)
        return 0;
    char *pp = p;
    for (; p!=path && p[0]!='/'; p--);
    if (p[0]=='/')
        p++;
    if (pp-p+1>MAXNAME-1)
        return 0;
    strncpy(name, p, pp-p+1);
    name[pp-p+1]=0;
    if (parent) {
        strncpy(parent, path, p-path);
        parent[p-path]=0;
    }
    return p;
}

// path = parent(dir)/name
// Create an inode and link it under "parent" with "name"
int myfs_mknod(char *path, u16 type) {
    u32 n;
    char parent[MAXPATH];
    char name[MAXNAME];
    struct dinode di;
    struct dirent de;
    if (!getname(path, name, parent)) {
        fprintf(stderr, "no name specified\n");
        return -1;
    }
    // Parent path must points to a valid *directory* inode.
    n = lookup(path, 1);
    if (n == NULLINUM) {
        fprintf(stderr, "parent directory not found %s\n", parent);
        return -1;
    }
    read_inode(n, &di);
    if (di.type != T_DIR) {
        fprintf(stderr, "not a directory %s\n", parent);
        return -1;
    }
    // Check for duplicates
    if (dir_lookup(n, name, 0)) {
        fprintf(stderr, "%s found under %s\n", name, parent);
        return -1;
    }
    // Create an inode.
    de.inum = alloc_inode(type);
    if (de.inum == NULLINUM) {
        fprintf(stderr, "failed to allocate inode\n");
        return -1;
    }
    // Link it to the "parent" dir.
    strncpy(de.name, name, MAXNAME);
    if (inode_write(n, &de, sizeof de, di.size) != sizeof de) {
        free_inode(de.inum);
        fprintf(stderr, "failed to write %s\n", parent);
        return -1;
    }
    // Inc link count to 1 cus now "parent" dir points to it.
    read_inode(de.inum, &di);
    di.linkcnt++;
    write_inode(de.inum, &di);
    return 0;
}

int myfs_open(char *path, u16 mode) {
    for (int i = 0; i < NFILES; i++) {
        if (opened[i].inum == NULLINUM) {
            int inum = lookup(path, 0);
            if (inum == NULLINUM)
                return -1;
            opened[i].inum = inum;
            opened[i].off = 0;
            opened[i].mode = mode;
            opened[i].refcnt = 1;
            return i;
        }
    }
    return -1;
}

int myfs_seek(int fd, u32 off) {
    if (opened[fd].inum == NULLINUM)
        return -1;
    opened[fd].off = off;
    return 0;
}

int myfs_write(int fd, void *buf, int sz) {
    if (!opened[fd].inum || !opened[fd].mode)
        return -1;
    u32 n = inode_write(opened[fd].inum, buf, sz, opened[fd].off);
    opened[fd].off += n;
    return n;
}

int myfs_read(int fd, void *buf, int sz) {
    if (!opened[fd].inum || opened[fd].mode & O_WRONLY)
        return -1;
    u32 n = inode_read(opened[fd].inum, buf, sz, opened[fd].off);
    opened[fd].off += n;
    return n;
}

// path=parent/name
// Remove the directory entry from "parent," and decrement
// the link count of the corresponding inode and free it
// if the link count reaches to 0.
int myfs_unlink(char *path) {
    u32 n;
    u32 nn;
    u32 off;
    struct dinode di;
    struct dirent de;
    char name[MAXNAME];
    char parent[MAXPATH];
    if (!getname(path, name, parent)) {
        fprintf(stderr, "no name specified\n");
        return -1;
    }
    // Parent path must points to a valid *directory* inode.
    n = lookup(path, 1);
    if (n == NULLINUM) {
        fprintf(stderr, "parent directory not found %s\n", parent);
        return -1;
    }
    read_inode(n, &di);
    if (di.type != T_DIR) {
        fprintf(stderr, "not a directory %s\n", parent);
        return -1;
    }
    // "name" must be in the parent directory
    nn = dir_lookup(n, name, &off);
    if ((nn = dir_lookup(n, name, &off)) ==  NULLINUM) {
        fprintf(stderr, "%s not found under %s\n", name, parent);
        return -1;
    }
    // Zero the directory entry found
    memset(&de, 0, sizeof de);
    assert(inode_write(n, &de, sizeof de, off) == sizeof de);
    // Decrement the link count cus "name" no longer points to that inode
    read_inode(nn, &di);
    di.linkcnt--;
    if (!di.linkcnt) {
        // Free the inode if link count reaches 0
        assert(free_inode(nn));
        return 0;
    }
    write_inode(nn, &di);
    return 0;
}

// Create a "new" path that points to the same inode the "old" path points to:
// Given old_parent/dirent{old_name, inum},
// create new_parent/dirent{new_name, inum}
int myfs_link(char *new, char *old) {
    u32 n;
    u32 nn;
    u32 off;
    struct dinode di;
    struct dirent de;
    char name[MAXNAME];
    char parent[MAXPATH];
    if (!getname(new, name, parent)) {
        fprintf(stderr, "no name specified\n");
        return -1;
    }
    // The "old" path must point a valid inode.
    if ((nn = lookup(old, 0)) != NULLINUM) {
        fprintf(stderr, "no such file or directory %s\n", old);
        return -1;
    }
    // Parent path must point to a *directory* inode.
    if ((n = lookup(new, 1)) == NULLINUM) {
        fprintf(stderr, "parent directory not found %s\n", parent);
        return -1;
    }
    read_inode(n, &di);
    if (di.type != T_DIR) {
        fprintf(stderr, "not a directory %s\n", parent);
        return -1;
    }
    // "name" must *not* be in "parent" already.
    if (dir_lookup(n, name, 0) != NULLINUM) {
        fprintf(stderr, "%s found under %s\n", name, parent);
        return -1;
    }
    // Create a new directory entry with "name"
    // the same inode number as "old"
    de.inum = nn;
    strncpy(de.name, name, MAXNAME);
    if (inode_write(n, &de, sizeof de, off) != sizeof de) {
        fprintf(stderr, "failed to append to dir\n");
        return -1;
    }
    // Increment the link count as now new also points to it.
    read_inode(nn, &di);
    di.linkcnt++;
    write_inode(nn, &di);
    return 0;
}

int myfs_close(int fd) {
    if (opened[fd].inum == NULLINUM) {
        fprintf(stderr, "invalid fd\n");
        return -1;
    }
    if (!--opened[fd].refcnt)
        opened[fd].inum = NULLINUM;
    return 0;
}

int myfs_stat(int fd, struct filestat *st) {
    if (opened[fd].inum == NULLINUM) {
        fprintf(stderr, "invalid fd\n");
        return -1;
    }
    struct dinode di;
    read_inode(opened[fd].inum, &di);
    st->type = di.type;
    st->linkcnt = di.linkcnt;
    st->size = di.size;
    return 0;
}
