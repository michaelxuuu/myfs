/**
 * @file testprog.c
 * @author
 * @brief 
 * @version 0.1
 * @date 2024-05-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "fs.h"

#include <stdio.h>

int main(int argc, char *argv[]) 
{
    fs_init(argv[1]);
    u32 src = 0xdeadbeef;
    u32 dest = 0;
    // create a file
    u32 i0 = alloc_inode(T_REG);
    for (int i = 0; i < 1; i++)
        inode_write(i0, &src, 4, 0);
    // link the file to root dir
    struct dirent i0file = {
        .inum = i0,
        .name = "i0file",
    };
    inode_write(ROOTINUM, &i0file, sizeof(i0file), 0);
    u32 i1 = fs_lookup("/i0file");
    inode_read(i1, &dest, 4, 0);
    printf("%x\n", dest);
}
