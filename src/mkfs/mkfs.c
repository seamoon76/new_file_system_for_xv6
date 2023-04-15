#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 12000//2000

// Disk layout:
// [ boot block | sb block | log | inode bitmap | inode blocks | free bit map | data blocks ]

int nibitmap = NINODES/(BSIZE*8) + 1; 
int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);
// new
void iappend_init_dir(uint inum, struct dirent *de);
void setibmap(int);
void updatesb(void);
void test_xpr(void);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum;
  struct dirent de;
  char buf[BSIZE];
  //struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap + nibitmap;
  nblocks = FSSIZE - nmeta;

  sb.magic = FSMAGIC;
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.ibmapstart = xint(2+nlog);
  sb.inodestart = xint(2+nlog+nibitmap);
  sb.bmapstart = xint(2+nlog+nibitmap+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u, inode bitmap blocks %u, inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, nibitmap,   ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
    // get rid of "user/"
    char *shortname;
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    assert(index(shortname, '/') == 0);

    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(shortname[0] == '_')
      shortname += 1;

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    //iappend(rootino, &de, sizeof(de));
    iappend_init_dir(rootino,&de);

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // // fix size of root inode dir
  // rinode(rootino, &din);
  // off = xint(din.size);
  // off = ((off/BSIZE) + 1) * BSIZE;
  // din.size = xint(off);
  // winode(rootino, &din);

  balloc(freeblock);
  setibmap(freeinode);
  updatesb();

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);

  //new
  din.rwmode = RW;
  din.supermode = 0;
  din.showmode = 1;

  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

void
die(const char *s)
{
  perror(s);
  exit(1);
}
uint cal_block_index_for_dinode(struct dinode* dp,unsigned int bn)
{

 if(bn<1)
  {
    die("bn=0 should be given to . and ..");
  }
  if(bn<OVERFLOW_BLOCK)
  return bn;
  die("cal_block_index: out of range");
  return 0;
}


// 在父目录dp下申请block，使得申请到的block是连续的
// 会改变dp->size的大小
// @return:addr of block related to 'off'
// 模仿bmap的写法
uint dir_block_allocation_continuation(struct dinode *dp, uint block)
{
  uint bn = block + 1, b = 0, baddr;
  for (; b < min(bn, NDIRECT); ++b)
  {
    if (dp->addrs[b] == 0)
      dp->addrs[b] = xint(freeblock++);
  }
  if (bn <= NDIRECT)
    baddr = xint(dp->addrs[bn - 1]);
  else
  {
    uint ibn = bn - NDIRECT;
    // Load indirect block, allocating if necessary.
    uint addr, indirect[NINDIRECT];
    if ((addr = dp->addrs[NDIRECT]) == 0)
      dp->addrs[NDIRECT] = addr = xint(freeblock++);
    rsect(xint(addr), (char *)indirect);
    for (b = 0; b < ibn; ++b)
    {
      if (indirect[b] == 0)
      {
        indirect[b] = xint(freeblock++);
        wsect(xint(addr), (char *)indirect);
      }
    }
    baddr = xint(indirect[ibn - 1]);
  }
  dp->size = bn * BSIZE > dp->size ? bn * BSIZE : dp->size;
  return baddr;
}

// 溢出区块连续化
uint overflow_block_allocation_continuation(struct dinode *dp, uint off)
{
  uint bn = off / BSIZE + 1, b = 0, baddr;
  for (; b < min(bn, NDIRECT); ++b)
  {
    if (dp->addrs[b] == 0)
      dp->addrs[b] = xint(freeblock++);
  }
  if (bn <= NDIRECT)
    baddr = xint(dp->addrs[bn - 1]);
  else
  {
    uint ibn = bn - NDIRECT;
    // Load indirect block, allocating if necessary.
    uint addr, indirect[NINDIRECT];
    if ((addr = dp->addrs[NDIRECT]) == 0)
      dp->addrs[NDIRECT] = addr = xint(freeblock++);
    rsect(xint(addr), (char *)indirect);
    for (b = 0; b < ibn; ++b)
    {
      if (indirect[b] == 0)
      {
        indirect[b] = xint(freeblock++);
        wsect(xint(addr), (char *)indirect);
      }
    }
    baddr = xint(indirect[ibn - 1]);
  }
  dp->size = bn * BSIZE > dp->size ? bn * BSIZE : dp->size;
  return baddr;
}

unsigned int BKDRHash_mkfs(char * str)
{
    unsigned int seed=131;
    unsigned int hash=0;
    while (*str)
    {
        hash=hash*seed+(*str++);

    }
    
    return (hash % HASHRANGE)+16;// HASHRANGE是哈希值范围
}
// @MaQi
// 初始化的时候将第一批文件的目录项*de 加入到inum对应的目录中
void iappend_init_dir(uint inum, struct dirent *de)
{
  uint tree_node_off = 0, tree_next_node_off,tree_node_block;
  struct dinode dir_inode;
  //unsigned int inner_node_off;
  unsigned int hash_value;
  
  char buf[BSIZE];//缓冲区

  // 读入inum对应的inode到dir_inode当中
  rinode(inum, &dir_inode);

  // while (tree_node_off < MAXFSIZE)
  // {
    hash_value=BKDRHash_mkfs(de->name);
    // if (tree_node_off<(hash_value+1)*4*sizeof(struct dirent))
    // {
      /*
      lower_bound_block=hash_value/16;
      tree_node_block=cal_block_index_for_dinode(&dir_inode,lower_bound_block);
      inner_node_off=hash_value%(NUM_DIRENT_PER_BLOCK/4)*4*sizeof(struct dirent);
      tree_node_off=tree_node_block*BSIZE+inner_node_off;
      tree_next_node_off=tree_node_off+4*sizeof(struct dirent);
      */

      tree_node_off=hash_value*16*sizeof(struct dirent);
      tree_next_node_off=(hash_value+1)*16*sizeof(struct dirent);
      tree_node_block=tree_node_off/BSIZE;

      //tree_next_node_off=min(cal_block_index_for_dinode(&dir_inode,lower_bound_block+1)*BSIZE,MAXFSIZE);
      // 这一区域需要block连续化
      
      tree_node_block=dir_block_allocation_continuation(&dir_inode,tree_node_block);
      
      //printf("iappend_init_dir: %s direct hash %d\n", de->name, hash_value);
    //}
    
    for (; tree_node_off < tree_next_node_off; tree_node_off += sizeof(struct dirent))
    {
      uint addr_of_block;
      // 理论上来说，这个里面的block应该是连续的
      // 所以这里还可以改进，申请只管这个块
      //如果是溢出区的访问，需要执行逐步块分配
      if(tree_node_off>=113*BSIZE)
      {
        addr_of_block=overflow_block_allocation_continuation(&dir_inode, tree_node_off);
      }
      else{
        addr_of_block = tree_node_block;
      }
      rsect(addr_of_block, buf);
      struct dirent *p_dirent = (struct dirent *)(buf + (tree_node_off % BSIZE));
      if (p_dirent->inum == 0)//如果这个地方的dirent是空的
      {
        bcopy(de, buf + (tree_node_off % BSIZE), sizeof(struct dirent));
        wsect(addr_of_block, buf);
        winode(inum, &dir_inode);//写入这个目录项的内容
        // 断言：偏置一定是16的倍数
        assert(tree_node_off % 16 == 0);
        //printf("iappend_init_dir: wrote %s at offset %d\n", de->name, tree_node_off);
        return;
      }
    }
    
      // overflow
      tree_node_off=113*BSIZE;
      tree_next_node_off=115*BSIZE;//MAXFSIZE;
      printf("iappend_init_dir: %s overflow\n", de->name);
    for (; tree_node_off < tree_next_node_off; tree_node_off += sizeof(struct dirent))
    {
      uint addr_of_block;
      // 理论上来说，这个里面的block应该是连续的
      // 所以这里还可以改进，申请只管这个块
      //如果是溢出区的访问，需要执行逐步块分配
      if(tree_node_off>=113*BSIZE)
      {
        addr_of_block=overflow_block_allocation_continuation(&dir_inode, tree_node_off);
      }
      else{
        addr_of_block = tree_node_block;
      }
      rsect(addr_of_block, buf);
      struct dirent *p_dirent = (struct dirent *)(buf + (tree_node_off % BSIZE));
      if (p_dirent->inum == 0)//如果这个地方的dirent是空的
      {
        bcopy(de, buf + (tree_node_off % BSIZE), sizeof(struct dirent));
        wsect(addr_of_block, buf);
        winode(inum, &dir_inode);//写入这个目录项的内容
        // 断言：偏置一定是16的倍数
        assert(tree_node_off % 16 == 0);
        printf("iappend_init_dir: wrote %s at offset %d\n", de->name, tree_node_off);
        return;
      }
    }
  //}
}

void
setibmap(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("setibmap: first %d inodes have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("setibmap: write ibitmap block at sector %d\n", sb.ibmapstart);
  wsect(sb.ibmapstart, buf);
}

void
updatesb(void)
{
  uchar buf[BSIZE];

  sb.freeinodes = xint(NINODES-freeinode);
  sb.freeblocks = xint(FSSIZE-freeblock);
  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);
}

void test_xpr(void)
{
  uchar buf[BSIZE];
  struct superblock _sb;

  rsect(1, buf);
  memmove(&_sb, buf, sizeof(_sb));
  printf("total blocks %u free blocks %u free inodes %u\n", xint(_sb.size), xint(_sb.freeblocks), xint(_sb.freeinodes));
}

