#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  if (chdir("ibmaptest_dir") < 0)
  {
    if (mkdir("ibmaptest_dir") < 0)
    {
      printf("mkdir failed\n");
      exit(1);
    }
    if (chdir("ibmaptest_dir") < 0)
    {
      printf("chdir failed\n");
      exit(1);
    }
  }

  char filename[4];
  int fd;
  filename[3] = '\0';
  int count = atoi(argv[1]);

  int start_tick = uptime();
  for (int i = 0x61; i <= 0x7a && count > 0; ++i)
  {
    filename[0] = i;
    for (int j = 0x61; j <= 0x7a && count > 0; ++j)
    {
      filename[1] = j;
      for (int k = 0x61; k <= 0x7a && count > 0; ++k, --count)
      {
        filename[2] = k;
        if ((fd = open(filename, O_CREATE,"iam@admin9876")) < 0)
        {
            printf("create empty file failed\n");
            exit(1);
        }
        close(fd);
      }   
    }
  }
  int end_tick = uptime();

  printf("create %d files in %d ticks\n", atoi(argv[1]), end_tick-start_tick);

  count = atoi(argv[1]);
  for (int i = 0x61; i <= 0x7a && count > 0; ++i)
  {
    filename[0] = i;
    for (int j = 0x61; j <= 0x7a && count > 0; ++j)
    {
      filename[1] = j;
      for (int k = 0x61; k <= 0x7a && count > 0; ++k, --count)
      {
        filename[2] = k;
        if (unlink(filename) < 0)
        {
            printf("unlink test file failed\n");
            exit(1);
        }
      }   
    }
  }
  if (chdir("..") < 0)
  {
    printf("chdir failed\n");
    exit(1);
  }
  if (unlink("ibmaptest_dir") < 0)
  {
    printf("remove testdir failed\n");
    exit(1);
  }

  exit(0);
}