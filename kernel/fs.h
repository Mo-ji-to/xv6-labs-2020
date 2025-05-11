// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

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
};

#define FSMAGIC 0x10203040

//#define NDIRECT 12    //直接块数量
#define NDIRECT 11    //直接块数量  相当于拿出一个直接块作为索引块
#define NINDIRECT (BSIZE / sizeof(uint))  //一级间接块数量
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT) //二级间接块数量

// On-disk inode structure

//磁盘的inode结构
struct dinode {
  short type;           // 文件类型 比如普通文件、目录文件、设备文件等
  short major;          // 主设备号
  short minor;          // 次设备号
  short nlink;          // 指向该索引节点的链接数目
  uint size;            // Size of file (bytes)
  //uint addrs[NDIRECT+1];   // 存储数据块的地址  11个直接盘块+一级索引表（包含256个盘块）
  uint addrs[NDIRECT+2];   // 存储数据块的地址  11个直接盘块+一级索引表+二级索引表
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

