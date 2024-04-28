#include "fs.h"

int main(int argc, char *argv[]) 
{
    fs_init(argv[1]);
    u32 src = 0xdeadbeef;
    u32 i0 = alloc_inode(T_REG);
    inode_write(i0, &src, 4, 0);
}
