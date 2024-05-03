#include "fs.h"

#include <stdio.h>
#include <string.h>

#define TEST_BSIZE 128
#define TEST_TEXT "Lily wakes up..."
#define TEST_TEXT_SZ strlen(TEST_TEXT)
#define TEST_FNAME "lily"

int main(int argc, char *argv[]) 
{
    fs_init(argv[1]);
    char b1[TEST_BSIZE] = TEST_TEXT;
    char b2[TEST_BSIZE] = {0};
    // create a file
    u32 i0 = alloc_inode(T_REG);
    // write b1 to the file
    inode_write(i0, &b1, TEST_TEXT_SZ, 0);
    // put the file under root dir
    struct dirent de = {
        .inum = i0,
        .name = TEST_FNAME,
    };
    inode_write(ROOTINUM, &de, sizeof(de), 0);
    // look up the file
    u32 i1 = fs_lookup("/" TEST_FNAME);
    // read from the file to see if it gives us the original text
    inode_read(i1, &b2, TEST_TEXT_SZ, 0);
    printf("%s\n", b2);
}
