#include "fs.h"

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

struct {
    int vd; // file desc pointing to the "virtual disk"
} fs;

// Disk (block) operations
// Write a disk block
void rblock(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(write(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}
// Read a disk block
void wblock(int n, char buf[BLOCKSIZE]) {
    assert(lseek(fs.vd, n * BLOCKSIZE, SEEK_SET) == n * BLOCKSIZE);
    assert(read(fs.vd, buf, BLOCKSIZE) == BLOCKSIZE);
}

// inode operations


int main() {

}
