#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int fd = open("lseektest.txt", O_CREATE|O_RDWR,"iam@admin9876");
  lseek(fd, atoi(argv[1]), atoi(argv[2]));
  if (write(fd, argv[3], strlen(argv[3])) != strlen(argv[3])) {
    fprintf(2, "lseektest write error\n");
    exit(1);
  }
  exit(0);
}
