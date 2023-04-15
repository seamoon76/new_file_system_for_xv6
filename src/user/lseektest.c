#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"


int
main(int argc, char *argv[])
{
  int fd = open("lseektest1.txt", O_CREATE|O_RDWR,"iam@admin9876");
  for(int i=0; i<20; i++){
      write(fd,"0",1);
  }


  lseek(fd, 4, SEEK_SET);
  write(fd, "111", 3);

  lseek(fd, 4, SEEK_CUR);
  write(fd, "222", 3);

  lseek(fd, -2, SEEK_END);
  write(fd, "333", 3);

  close(fd);
  exit(0);
}