#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   //new symbolic link

#define T_EXTENT 5  // new extent way

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
  uint rwmode; //读写权限
  uint supermode; //是否是高级文件
  uint showmode; //是否显示
  uint addrs[13]; // new
};
