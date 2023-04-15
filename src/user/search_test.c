
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#define CREATE_FILE_NUM 150
// todo:把文件性能测试写到一起
int create_files()
{
  int start_tick = uptime();

  char filename[4];
  int fd;
  filename[3] = '\0';
  for (int i = 0x61; i <= 0x79; ++i)
  {
    filename[0] = i;
    for (int j = 0x61; j <= 0x66; ++j)
    {
      filename[2] = j;
      filename[1] = j+j%0x9;
      if ((fd = open(filename, O_CREATE,"iam@admin9876")) < 0)
      {
        printf("create empty file failed\n");
        exit(1);
      }
      close(fd);
    }
  }

  int end_tick = uptime();
  return end_tick - start_tick;
}

int ls()
{
  
  char *argv[] = {"ls",0};
  int start_tick = uptime();
  if (!fork())
  {
    if (exec("../ls", argv) < 0)
    {
      printf("ls failed\n");
      exit(1);
    }
    exit(0);
  }
  else
  {
    int status;
    wait(&status);
    if (status != 0)
      exit(status);
  }
  int end_tick = uptime();
  return end_tick - start_tick;
}

int clear()
{
  int v = 0;
  char filename[4];
  filename[3] = '\0';
  for (int i = 0x61; i <= 0x79; ++i)
  {
    filename[0] = i;
    for (int j = 0x61; j <= 0x66; ++j)
    {
      filename[2] = j;
      filename[1] = j+j%0x9;
      if (unlink(filename) < 0 && v == 0)
      {
        printf("clear failed\n");
        v = -1;
      }
    }
  }
  return v;
}

int search_files()
{
  char filename[4], fn_0[25], fn_1[6];
  filename[3] = '\0';
  for (int i = 0; i < 25; ++i)
    fn_0[i] = i + 0x61;
  for (int i = 0; i < 6; ++i)
    fn_1[i] = i + 0x61;

  int start_tick = uptime();
  for (int i = 0; i < 25; ++i)
  {
    filename[0] = fn_0[i];
    for (int j = 0; j < 6; ++j)
    {
      filename[2] = fn_1[j];
      filename[1] = fn_1[j]+fn_1[j]%9;

      char *argv[] = {"cat", filename, 0};
      if (!fork())
      {
        int ret = exec("../cat", argv);
        if (ret < 0)
        {
          printf("cat failed with code %d\n", ret);
          exit(1);
        }
        exit(0);
      }
      else
      {
        int status;
        wait(&status);
        if (status != 0)
          exit(status);
      }
    }
  }

  int end_tick = uptime();
  return end_tick - start_tick;
}

int main()
{
  // 准备
  if (chdir("search_test_dir") < 0)
  {
    if (mkdir("search_test_dir") < 0)
    {
      printf("mkdir failed\n");
      exit(1);
    }
    if (chdir("search_test_dir") < 0)
    {
      printf("chdir failed\n");
      exit(1);
    }
  }

  // 计算目录查找开销
  printf("begin test search ...\n");
  uint create_time_cost=create_files();
  printf("create %d files: total time cost %d ticks\n", CREATE_FILE_NUM,create_time_cost);
  printf("ls:time cost %d ticks\n", ls());
  printf("random search files used %d ticks\n", search_files());
  if (clear() < 0)
    exit(1);

  // 清理
  if (chdir("..") < 0)
  {
    printf("chdir failed\n");
    exit(1);
  }
  if (unlink("search_test_dir") < 0)
  {
    printf("remove testdir failed\n");
    exit(1);
  }
  exit(0);
}