// Defines the disk layout

// reserved (for booting) | super block | log blocks | inode blocks | bitmap block | data blocks

// Fixing the below paramters and everything else can be derived:
#define BLOCKSIZE 512
#define NBLOCKS_TOT 1024 // Disk size 512k
#define NBLOCKS_RES 64   // Reserve 64 blocks (32k) for booting (MBR and bootloader)
#define NBLOCKS_LOG 30
#define NINODES 200
