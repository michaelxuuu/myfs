#include "fs.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST_FNAME "test"

static void to_myfs(const char *name, u32 inum) {
    int fd = open(name, O_RDWR, 0644);
    assert(fd >= 0);
    char c;
    u32 off = 0;
    for (;;) {
        int n;
        assert((n = read(fd, &c, 1)) >= 0);
        if (!n)
            break;
        printf("%d\n", off);
        assert(inode_write(inum, &c, 1, off++));
    }
    close(fd);
}

static void to_hostfs(u32 inum, const char *name) {
    int fd = open(name, O_CREAT | O_TRUNC | O_RDWR , 0644);
    assert(fd >= 0);
    char c;
    u32 off = 0;
    for (;;) {
        int n;
        assert((n = inode_read(inum, &c, 1, off++)) >= 0);
        if (!n)
            break;
        assert(write(fd, &c, 1));
    }
    close(fd);
}

int main(int argc, char *argv[]) 
{
    fs_init(argv[1]);
    // create a file
    u32 i0 = alloc_inode(T_REG);
    // migrate the test file from the host fs to my fs
    to_myfs("random.txt", i0);
    // put the file under root dir
    struct dirent de = {
        .inum = i0,
        .name = TEST_FNAME,
    };
    inode_write(ROOTINUM, &de, sizeof(de), 0);
    // look up the file
    u32 i1 = fs_lookup("/" TEST_FNAME);
    // read from the file to see if it gives us the original text
    to_hostfs(i1, "random1.txt");
}
