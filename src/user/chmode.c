#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "unvalid input!");
    exit(1);
  }

  chmode(argv[1],atoi(argv[2]));

  exit(0);
}