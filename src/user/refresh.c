#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


char *
fmtname(char *path)
{
  static char buf[DIRSIZ + 1];
  char *p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if (strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  return buf;
}

void refresh(char *path)
{
  char buf[512], *p,buf_leaf[257];
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0, "iam@admin9876")) < 0)
  {
    fprintf(2, "refresh: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0)
  {
    fprintf(2, "refresh: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type)
  {
  case T_FILE:

  if(st.showmode == 0){
    show(path);
    printf("successfully refreshed\n");
    break;
  }
  else{
    printf("already fresh\n");
    break;
  }

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
      printf("refresh: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    // @author:maqi
    // 按照叶块来读,一次读入1个叶块，16个目录项
    while(read(fd, buf_leaf, 16*sizeof(de)) == 16*sizeof(de)){
      for(int i=0;i<16;i++)
      {
        memcpy((char*)&de,buf_leaf+i*sizeof(de),sizeof(de));
        //strncpy(buf_leaf+i*sizeof(de),(char*)&de,sizeof(de));
        if(de.inum == 0)
          break;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
          printf("recyclelist: cannot stat %s\n", buf);
          continue;
        }
        
        if(st.showmode == 0){
          show(path);
          printf("successfully refreshed\n");
          break;
        }
        else{
            printf("already fresh\n");
            break;
        }
      }
    }
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  int i;

  if (argc < 2)
  {
    printf("error: invalid input\n");
  }

  for (i = 1; i < argc; i++)
    refresh(argv[i]);
  exit(0);
}