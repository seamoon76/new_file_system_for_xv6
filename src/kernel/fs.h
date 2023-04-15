// On-disk file system format.
// Both the kernel and user programs use this header file.

#define ROOTINO 1  // root i-number
#define BSIZE 1024 // block size

// new
#define RW 3   // 11
#define W_ 1   // 01
#define R_ 2   // 10
#define NONE 0 // 00

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
  uint ibmapstart;
  uint freeinodes;
  uint freeblocks;
};

extern struct superblock sb;

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))

// new
// #define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)
#define MAXFILE   (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT + NINDIRECT * NINDIRECT * NINDIRECT)

// On-disk inode structure
struct dinode
{
  short type;  // File type
  short major; // Major device number (T_DEVICE only)
  short minor; // Minor device number (T_DEVICE only)
  short nlink; // Number of links to inode in file system
  uint size;   // Size of file (bytes)

  // new
  uint rwmode;    // 读写权限，00不能读不能写，01不能读可以写，10可读不能写，11能读能写（default）
  uint supermode; //是否是高级文件
  uint addrs[NDIRECT + 3];
  uint showmode; //是否显示
  uchar useless[41];
};

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB (BSIZE * 8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

// Block of free inode map containing bit for inode i
#define IBBLOCK(i, sb) ((i)/BPB + sb.ibmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

#define DIRENT_NUM 7168
#define FIRST_LAYER_NODE_NUM 112
#define OVERFLOW_BLOCK FIRST_LAYER_NODE_NUM+1
//#define OVERFLOW_DIRENT_NUM  704//((17148-64)/64-256)*64=11*64
#define NUM_DIRENT_PER_BLOCK BSIZE/sizeof(struct dirent)
#define SECOND_LAYER_NODE_NUM NUM_DIRENT_PER_BLOCK
#define HASHRANGE 443 // 0x1BB //0x6FD // 1789
#define MAXFSIZE (119 * BSIZE)
