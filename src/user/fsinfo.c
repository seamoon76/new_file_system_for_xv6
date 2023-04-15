#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int
main(int argc, char *argv[])
{
  struct superblock _sb;

  if(fsinfo(&_sb) < 0){
    fprintf(2, "fsinfo: failed to access superblock\n");
    exit(1);
  }
  printf("total blocks\t%d\n", _sb.size);
//   printf("meta blocks\t%d\n", _sb.size - _sb.nblocks);
//   printf("data blocks\t%d\n", _sb.nblocks);
  printf("free blocks\t%d\n", _sb.freeblocks);
  printf("total inodes\t%d\n", _sb.ninodes);  
  printf("free inodes\t%d\n", _sb.freeinodes);   
  exit(0);
}