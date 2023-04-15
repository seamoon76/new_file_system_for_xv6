// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 
struct spinlock sblock;

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

static void
updatesb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(bp->data, sb, sizeof(*sb));
  log_write(bp);
  brelse(bp);
}


// Init fs
void
fsinit(int dev) {
  initlock(&sblock, "superblock");
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        acquire(&sblock);
        sb.freeblocks--;
        release(&sblock);
        updatesb(dev, &sb);        
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
  acquire(&sblock);
  sb.freeblocks++;
  release(&sblock);
  updatesb(dev, &sb);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct
{
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void iinit()
{
  int i = 0;

  initlock(&itable.lock, "itable");
  for (i = 0; i < NINODE; i++)
  {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);


// search for a free inode in inode bitmap and mark it as used
static int
searchibmap(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.ninodes; b += BPB){
    bp = bread(dev, IBBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.ninodes; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is inode free?
        bp->data[bi/8] |= m;  // Mark inode in use.
        log_write(bp);
        brelse(bp);
        acquire(&sblock);
        sb.freeinodes--;
        release(&sblock);
        updatesb(dev, &sb);        
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("ialloc: out of free inodes");
}

// Free an inode.
static void
ifree(int dev, int inum)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, IBBLOCK(inum, sb));
  bi = inum % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free inode");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
  acquire(&sblock);
  sb.freeinodes++;
  release(&sblock);
  updatesb(dev, &sb);
}


// Allocate an inode on device dev.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;


  inum = searchibmap(dev);
  bp = bread(dev, IBLOCK(inum, sb));
  dip = (struct dinode*)bp->data + inum%IPB;
  memset(dip, 0, sizeof(*dip));
  dip->type = type;

  // new
      dip->rwmode = RW;
      dip->supermode = 0;
      dip->showmode = 1;

  log_write(bp);   
  brelse(bp);
  return iget(dev, inum);
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;

  // new
  dip->rwmode = ip->rwmode;
  dip->supermode = ip->supermode;
  dip->showmode = ip->showmode;

  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode *
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;

    // new
    ip->rwmode = dip->rwmode;
    ip->supermode = dip->supermode;
    ip->showmode = dip->showmode;

    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip);
    ifree(ip->dev, ip->inum);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // new
  // 使用extent分配有两个关键要素，起始指针和长度
  uint p_extent_file,len_extent_file;
  
  if(ip->type==T_EXTENT){
    //printf("debug:use extent!");
    int i = 0;
    // 找磁盘上的地址 
    while(ip->addrs[i]!=0){
      len_extent_file = ip->addrs[i] & 0xff; // ip的addr的最后4位表示文件长度，模256可以取出最后4位 
      if(bn >= ip->addrs[i+1] && bn < ip->addrs[i+1] + len_extent_file){ //第n块在起点到起点+length之间，不需要分配新块
        return (/*len_extent_file*/(ip->addrs[i] & ~ 0xff) /256) + bn - ip->addrs[i+1]; //第几个exten+单个extent内部的值
      }
      i++;
    }
    // 需要分配新块
    addr = balloc(ip->dev);
    if(i > 0){
      len_extent_file = ip->addrs[i-1] &0xff; // 取出地址后四位，得到文件长度
      p_extent_file = (ip->addrs[i-1] & ~ 0xff) /256; // 前12位确定是第几个extent,也就是起始指针
      if(addr == p_extent_file + len_extent_file){// 地址前12位是指针，后4位是长度
        ip->addrs[i-1] = (p_extent_file *256 | (len_extent_file + 1));
        return addr;
      }
    }
    
    ip->addrs[i] = (addr *256 | 1); // 地址16位，地址前12位是指针，后4位是长度
    ip->addrs[i+1] = bn;
    return addr;
  }
  else{
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  // new 二级索引
  bn -= NINDIRECT;
  if (bn < NINDIRECT * NINDIRECT)
  {
    if ((addr = ip->addrs[NDIRECT + 1]) == 0)
    {
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    }
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[bn / NINDIRECT]) == 0)
    {
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[bn % NINDIRECT]) == 0)
    {
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  // 三级索引
  bn -= NINDIRECT * NINDIRECT;
  if (bn < NINDIRECT * NINDIRECT * NINDIRECT)
  {
    if ((addr = ip->addrs[NDIRECT + 2]) == 0)
    {
      ip->addrs[NDIRECT + 2] = addr = balloc(ip->dev);
    }
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;

    if ((addr = a[bn / (NINDIRECT * NINDIRECT)]) == 0)
    {
      a[bn / (NINDIRECT * NINDIRECT)] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[(bn % (NINDIRECT * NINDIRECT)) / NINDIRECT]) == 0)
    {
      a[(bn % (NINDIRECT * NINDIRECT)) / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[(bn % (NINDIRECT * NINDIRECT)) % NINDIRECT]) == 0)
    {
      a[(bn % (NINDIRECT * NINDIRECT)) % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  panic("bmap: out of range");
  }
  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;
  // new
  // 使用extent分配有两个关键要素，起始指针和长度
  uint /*p_extent_file,*/len_extent_file;
  
  if(ip->type==T_EXTENT){
    int i = 0;
    // 找磁盘上的地址 
    int addr_block_to_free=-1;
    while(ip->addrs[i]!=0){
      len_extent_file = ip->addrs[i] %256; // ip的addr的最后4位表示文件长度，模256可以取出最后4位 
      
      for(int bn = ip->addrs[i+1]; bn < ip->addrs[i+1] + len_extent_file;bn++){ //第n块在起点到起点+length之间，不需要分配新块
        addr_block_to_free=((ip->addrs[i] & ~ 0xff) /256)  + bn - ip->addrs[i+1]; //第几个exten+单个extent内部的值
        
        // printf("debug:*******************\n");
        // printf("i=%d,ip->addrs[i]=%d,bn=%d,ip->addrs[i+1]=%d,ptr=%d\n",i,ip->addrs[i],bn,ip->addrs[i+1],(ip->addrs[i] & ~ 0xff) /256);
        // printf("to free block addr:%d\n",addr_block_to_free);
        bfree(ip->dev,addr_block_to_free);
      }
      ip->addrs[i] = 0;
      i++;
      if(i>NDIRECT+2)//可能用得上，现在还不知道是否能
      {
        break;
      }
    }
    // // 需要分配新块
    // addr = balloc(ip->dev);
    // if(i > 0){
    //   len_extent_file = ip->addrs[i-1] %256; // 取出地址后四位，得到文件长度
    //   p_extent_file = len_extent_file /256; // 前12位确定是第几个extent,也就是起始指针
    //   if(addr == p_extent_file + len_extent_file){// 地址前12位是指针，后4位是长度
    //     ip->addrs[i-1] = (p_extent_file *256 | (len_extent_file + 1));
    //     return addr;
    //   }
    // }
    
    // ip->addrs[i] = (addr *256 | 1); // 地址16位，地址前12位是指针，后4位是长度
    // ip->addrs[i+1] = bn;
    // return addr;
  }
  else{
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // new
  //二级索引
  if (ip->addrs[NDIRECT + 1])
  {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++)
    {
      if (a[j])
      {
        struct buf *second_bp;
        uint *second_a;
        second_bp = bread(ip->dev, a[j]);
        second_a = (uint *)second_bp->data;

        // Second layer.
        for (int k = 0; k < NINDIRECT; k++)
        {
          if (second_a[k])
          {
            bfree(ip->dev, second_a[k]);
          }
        }
        brelse(second_bp);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  // new
  //三级索引
  if (ip->addrs[NDIRECT + 2])
  {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 2]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++)
    {
      if (a[j])
      {
        struct buf *second_bp;
        uint *second_a;
        second_bp = bread(ip->dev, a[j]);
        second_a = (uint *)second_bp->data;

        // Second layer.
        for (int k = 0; k < NINDIRECT; k++)
        {
          if (second_a[k])
          {
            struct buf *third_bp;
            uint *third_a;
            third_bp = bread(ip->dev, second_a[k]);
            third_a = (uint *)third_bp->data;

            // Third layer
            for (int l = 0; l < NINDIRECT; l++)
            {
              if (third_a[l])
              {
                bfree(ip->dev, third_a[l]);
              }
            }
            brelse(third_bp);
            bfree(ip->dev, second_a[k]);
          }
        }
        brelse(second_bp);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }
  }
  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;

  // new by ply
  st->rwmode = ip->rwmode;
  st->supermode = ip->supermode;
  st->showmode = ip->showmode;
  // new by maqi
  for(int i = 0; i < 13; i++){
    st->addrs[i] = ip->addrs[i];
  }
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Hash Tree
// @author:MaQi
// @Modify time:2022/5/1
// @hash function
// @param:
//  + str: name of file
// @return : hash value in range[0,16380]
unsigned int BKDRHash(char * str)
{
    unsigned int seed=131;
    unsigned int hash=0;
    while (*str)
    {
        hash=hash*seed+(*str++);

    }
    
    return (hash % HASHRANGE)+16;// HASHRANGE是哈希值范围
}

// 相当于bmap的只计算不申请
// 返回n对应的树第一层B树的节点
// 第一层共有256个节点，每个节点是64项，根据n计算n属于的那个范围，确定出来这个block(64个dirent)
/*
直接块
第0block . ..
1-11block 一层0~10节点
12存储1级链接
间接块
0~244,11~255
溢出
245~255,0~10
*/
uint cal_block_index(struct inode* dp,unsigned int bn)
{

  // 第一个block要留给'.'和'..'
  // uint addr,*a;
  // struct buf *bp;
  if(bn<1)
  {
    panic("bn=0 should be given to . and ..");
  }
  if(bn<OVERFLOW_BLOCK)
  return bn;
  panic("cal_block_index: out of range");
  /*
  if(bn<NDIRECT)//1~11
  {
    // addr=dp->addrs[bn];
    return addr;
  }
  //bn>=12
  bn-=NDIRECT;
  // 间接块和溢出都会在这里，bn最大可以达到265
  if(bn<102 NINDIRECT)
  {
    return bn+NDIRECT;
    
    if ((addr = dp->addrs[NDIRECT]) == 0)
      dp->addrs[NDIRECT] = addr = balloc(dp->dev);
    bp = bread(dp->dev, addr);
    a = (uint *)bp->data;
    addr=a[bn];
    // if ((addr = a[bn]) == 0)
    // {
    //   a[bn] = addr = balloc(ip->dev);
    //   log_write(bp);
    // }
    brelse(bp);
    return addr;

  }
  */
}

void check_log_full()
{
  int logfull=(if_log_full()==0);
    if(logfull)
    {
      end_op();
      begin_op();
    }
}

int check_free()
{
  int remain1=(if_log_full()==1);
  return remain1;
}


// 连续分配block直到一直分配到指定off字节。
// 返回值：申请的块数
uint contiguous_block_allocation(struct inode *ip, uint off)
{
  uint block_end_id=off/(uint)BSIZE;//需要连续分配到多少
  uint first_block = ip->size/(uint)BSIZE;
  if(ip->size % (uint)BSIZE !=0)
  {
    first_block+=1;
  }
  if (first_block>block_end_id){
    // 需要分配的块已经在已经分配的块内了，不需要分配
    return 0;
  }
  
  uint block_id = first_block;
  while(block_id<=block_end_id&&block_id<=NDIRECT-1)
  {
    // 分配块
    ip->addrs[block_id] = balloc(ip->dev);
    check_log_full();
    block_id++;
  }
  
  // 如果直接块就足够满足分配了，很简单，不需要额外申请，直接返回
  if(block_end_id<NDIRECT)
  {
    return block_end_id - first_block + 1;
  }

  // 直接块不能满足需要，那就到间接块中寻找，注意，虽然有
  // 二三级链接，但是目录文件用不了那么多，其实这里限制了目录下文件数量，是直接块+间接块=268个目录项，
  // 如果想要扩大目录下文件数量，请修改这里

  uint block_addr=ip->addrs[NDIRECT],*p_data=0;
  if(block_addr==0)
  {
    // 说明最后一个直接块还没使用
    ip->addrs[NDIRECT] = balloc(ip->dev);
    block_addr = ip->addrs[NDIRECT];

    check_log_full();
  }
  
  struct buf *pointer_buf = bread(ip->dev, block_addr);
    p_data = (uint *)pointer_buf->data;
    while(block_id <= block_end_id)
    {
      p_data[block_id-NDIRECT] = balloc(ip->dev);
      if(check_free())
      {
        log_write(pointer_buf);
        brelse(pointer_buf);
        end_op();
        begin_op();
        pointer_buf=bread(ip->dev,block_addr);
        p_data=(uint*)pointer_buf->data;
      }
      block_id++;
    }
  log_write(pointer_buf);
  brelse(pointer_buf);
  
  return block_end_id - first_block + 1;
}

// todo!!
// 把dp->size的大小扩容至指定大小
uint tree_expand(struct inode *ip, uint size)
{
  uint allocated_block_num = contiguous_block_allocation(ip, size);
  uint old_block_num = (ip->size-1)/BSIZE;
  if(ip->size<1)
  {
    old_block_num=0;
  }
  uint inner_off=ip->size%(uint)BSIZE;

  uint right = size + 1;
  if(allocated_block_num!=0)right=old_block_num*BSIZE+BSIZE;

  if(inner_off==0){
    ip->size = size + 1;
    iupdate(ip);
    return size;
  }
  
  struct buf *p_buf=bread(ip->dev,bmap(ip,old_block_num));
  memset(p_buf->data + inner_off, 0, right-ip->size);
  log_write(p_buf);
  brelse(p_buf);
  
  ip->size = size + 1;
  iupdate(ip);
  return size;
}

// tree_search
// @author: MaQi
// @param:
//  + dp:父目录，将在该目录下查找
//  + name: 要查找的目录项
//  + poff: 找到的位置对应的偏移量
//  + de: 目录项指针
// 返回值：如果找到了dirent，计算结果将保存在de中，并把偏移量写在poff中，
//         如果没有找到，把de->inum设置为0
void tree_search(struct inode* dp, char* name, uint* poff, struct dirent* de,uint add)
{
  
  uint tree_node_off=0, tree_next_node_off=0;//,tree_node_block=1;// 以字节为单位的内存偏移量
  unsigned int hash_value;
  /*
  unsigned int lower_bound_block;
  unsigned int inner_node_off;//[0,15]*sizeof(de)
  */
  //uint tree_node_index=0;
  //while(tree_node_off<MAXFSIZE){
  //printf("in tree_search: tree_node_off%d dp->size:%d\n",tree_node_off,dp->size);
  hash_value=BKDRHash(name);
  //printf("name%s\n",name);
    //printf("hash_value:%d\n",hash_value);
    // 计算哈希值[64,17137-1+64]

    /*
    // 确定哈希值对应的哈希范围，然后确定block，一个block有64个dirent，
    // lower_bound>=1,[1,256+1)
    //  printf("NUM_DIRENT_PER_BLOCK:%d\n",NUM_DIRENT_PER_BLOCK);
    lower_bound_block=hash_value/16;
    //   printf("lower_bound_block:%d\n",lower_bound_block);
    inner_node_off=hash_value%(NUM_DIRENT_PER_BLOCK/4)*4*sizeof(struct dirent);
    
    // upper_bound=lower_bound+NUM_DIRENT_PER_BLOCK;
    // 第一块要留给'.'和'..'
    tree_node_block=cal_block_index(dp,lower_bound_block);
    
    tree_node_off=tree_node_block*BSIZE+inner_node_off;
    tree_next_node_off=tree_node_off+4*sizeof(struct dirent);
    */
    tree_node_off=hash_value*16*sizeof(struct dirent);
    tree_next_node_off=(hash_value+1)*16*sizeof(struct dirent);

        
    //printf("tree_node_off:%d\n",tree_node_off);
    //printf("tree_next_node_off:%d\n",tree_next_node_off);
    if(tree_node_off>MAXFSIZE)
    {
      printf("name:%s\n",name);
          printf("hash_value:%d\n",hash_value);
          //printf("lower_bound_block:%d\n",lower_bound_block);
          //printf("tree_node_block:%d\n",tree_node_block);
          printf("tree_node_off:%d\n",tree_node_off);
          printf("tree_next_node_off:%d\n",tree_next_node_off);
          printf("dp->size:%d\n",dp->size);
      panic("off bigger than MAXFSSIZE\n");
    }
    //tree_next_node_off=min(cal_block_index(dp,lower_bound_block+1)*BSIZE,MAXFSIZE);


    // debug
    // if(add==1)
    //       {
    //         printf("in add file mode. \n");
    //       }
    //       else{
    //         printf("in find file mode. \n");
    //       }
    // printf("name:%s\n",name);
    //       printf("hash_value:%d\n",hash_value);
    //       printf("lower_bound_block:%d\n",lower_bound_block);
    //       printf("tree_node_block:%d\n",tree_node_block);
    //       printf("tree_node_off:%d\n",tree_node_off);
    //       printf("tree_next_node_off:%d\n",tree_next_node_off);
    //       printf("dp->size:%d\n",dp->size);
    
    
    // 在这个block内部进行线性查找（不一定要按照线性，先按照线性写
    for(;tree_node_off<tree_next_node_off;tree_node_off+=sizeof(struct dirent))
    {
        // 根据需要进行扩容，搜索的话不需要扩容，而新建的话需要扩容
        if(tree_node_off>=dp->size){
          if(add==1){
            //debug
            //printf("off %d, stop_off %d, pre_size %d\n",tree_node_off,tree_next_node_off,dp->size);
            tree_expand(dp,tree_next_node_off-1);
            //printf("now_size %d\n",dp->size);
          }
          else{
            de->inum=0;
            return;
          }
        }
        
        if (readi(dp, 0, (uint64)de, tree_node_off, sizeof(struct dirent)) != sizeof(struct dirent)){
          if(add==1)
          {
            printf("in add file mode\n");
          }
          else{
            printf("in find file mode\n");
          }
          printf("name:%s\n",name);
          printf("hash_value:%d\n",hash_value);
          //printf("lower_bound_block:%d\n",lower_bound_block);
          //printf("tree_node_block:%d\n",tree_node_block);
          printf("tree_node_off:%d\n",tree_node_off);
          printf("dp->size:%d\n",dp->size);


        panic("tree_search");
        }
        //printf("in for loop and have red de\n");
        if(!add)//查找模式下
        {
          if (namecmp(de->name,name)==0)
          {
            if (poff){
              *poff = tree_node_off;
              
            }
            // printf("find %s at offset %d\n",name,tree_node_off);
            return;
          }
          // else if(tree_node_off<113*BSIZE){
          //   printf("at %d is %s not %s\n",tree_node_off,de->name,name);
          // }
        }
        else{// 新建模式下
          if(de->inum==0)//是空的，可以用
          {
            
            if (poff)
              *poff = tree_node_off;
            //printf("%d is empty can use\n",tree_node_off);
            return;
          }
          // else{
          //   printf("this is %s not empty\n",de->name);
          // }
        }
    }
    
    // 在常规区域没有找到
    // 到溢出区找
    tree_node_off=113*BSIZE;
    tree_next_node_off=115*BSIZE;//MAXFSIZE;
    //printf("search in overflow area.\n");
    for(;tree_node_off<tree_next_node_off;tree_node_off+=sizeof(struct dirent))
    {
        // 根据需要进行扩容，搜索的话不需要扩容，而新建的话需要扩容
        if(tree_node_off>=dp->size){
          if(add==1){
            //debug
            //printf("off %d, stop_off %d, pre_size %d\n",tree_node_off,tree_next_node_off,dp->size);
            tree_expand(dp,tree_next_node_off-1);
            //printf("now_size %d\n",dp->size);
          }
          else{
            de->inum=0;
            return;
          }
        }
        
        if (readi(dp, 0, (uint64)de, tree_node_off, sizeof(struct dirent)) != sizeof(struct dirent)){
          if(add==1)
          {
            printf("in add file mode\n");
          }
          else{
            printf("in find file mode\n");
          }
          printf("name:%s\n",name);
          printf("hash_value:%d\n",hash_value);
          //printf("lower_bound_block:%d\n",lower_bound_block);
          //printf("tree_node_block:%d\n",tree_node_block);
          printf("tree_node_off:%d\n",tree_node_off);
          printf("dp->size:%d\n",dp->size);


        panic("tree_search");
        }
        //printf("in for loop and have red de\n");
        if(!add)//查找模式下
        {
          if (namecmp(de->name,name)==0)
          {
            if (poff){
              *poff = tree_node_off;
              
            }
            //printf("find %s at offset %d\n",name,tree_node_off);
            return;
          }
          // else{
          //   printf("at %d is %s not %s\n",tree_node_off,de->name,name);
          // }
        }
        else{// 新建模式下
          if(de->inum==0)//是空的，可以用
          {
            if (poff)
              *poff = tree_node_off;
            return;
          }
          // else{
          //   printf("this is %s not empty\n",de->name);
          // }
        }
    }
    
    // //在溢出区内查找并且遍历了一遍溢出区，没有返回，说明溢出区满了
    // if(tree_next_node_off==MAXFSIZE){
    //   if(add)
    //     printf("overflow area is full\n");
    //   break;
    // }
    
  //}
  de->inum=0;//查找失败
  
}


void tree_add(struct inode* dp, char* name)
{

}



// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");
  if(namecmp(name,".")!=0&&namecmp(name,"..")!=0)
  {
    
    tree_search(dp,name,poff,&de,0);
    if(de.inum)
    {
      return iget(dp->dev,de.inum);
    }
    //panic("done 770:not found");
  }
  else{
    // "."或者".."直接线性查找前两个。
  for(off = 0; off < 2*sizeof(de); off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }
  //panic("done 787");
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  uint off=0;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  tree_search(dp,name,&off,&de,1);
  // printf("create file %s\n",name);
  
  // for(off = 0; off < dp->size; off += sizeof(de)){
  //   if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
  //     panic("dirlink read");
  //   if(de.inum == 0)
  //     break;
  // }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup_old(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// // Write a new directory entry (name, inum) into the directory dp.
// int dirlink(struct inode *dp, char *name, uint inum)
// {
//   int off;
//   struct dirent de;
//   struct inode *ip;

//   // Check that name is not present.
//   if ((ip = dirlookup(dp, name, 0)) != 0)
//   {
//     iput(ip);
//     return -1;
//   }

//   // Look for an empty dirent.
//   for(off = 0; off < dp->size; off += sizeof(de)){
//     if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
//       panic("dirlink read");
//     if(de.inum == 0)
//       break;
//   }

//   strncpy(de.name, name, DIRSIZ);
//   de.inum = inum;
//   if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
//     panic("dirlink");

//   return 0;
// }
// */


// 初始化目录
void build_dir_tree(struct inode* dp,uint pinum)
{
  dp->size=2*sizeof(struct dirent);
  iupdate(dp);

  struct dirent de;
  //写入"."
  if (readi(dp, 0, (uint64)&de, 0, sizeof(struct dirent)) != sizeof(struct dirent))
      panic("dirinit read");
  strncpy(de.name,".",DIRSIZ);
  de.inum=dp->inum;
  if (writei(dp, 0, (uint64)&de, 0, sizeof(de)) != sizeof(de))
      panic("dirinit write");
  
  //写入".."
  if (readi(dp, 0, (uint64)&de, sizeof(de), sizeof(struct dirent)) != sizeof(struct dirent))
      panic("dirinit read");
  strncpy(de.name,"..",DIRSIZ);
  de.inum=pinum;
  if (writei(dp, 0, (uint64)&de, sizeof(de), sizeof(de)) != sizeof(de))
      panic("dirinit write");

}



// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else
  {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while ((path = skipelem(path, name)) != 0)
  {
    ilock(ip);
    if (ip->type != T_DIR)
    {
      iunlockput(ip);
      return 0;
    }
    if (nameiparent && *path == '\0')
    {
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if ((next = dirlookup(ip, name, 0)) == 0)
    {
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if (nameiparent)
  {
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode *
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode *
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

//new
struct inode *
divesymlink(struct inode *isym)
{
    struct inode *ip = isym;
    char path[MAXPATH + 1];
    uint len;
    int deep = 0;

    do {
        // get linked target
        // we don't know how long the file str is, so we expect once read could get fullpath.
        len = readi(ip, 0, (uint64)path, 0, MAXPATH);
        if (readi(ip, 0, (uint64)(path + len), len, MAXPATH + 1) != 0)
            panic("divesymlink : short read");    

        iunlockput(ip);
        if (++deep > 10) {  // may cycle link
           return 0;
        }

        if ((ip = namei((char *)path)) == 0) { // link target not exist
            return 0;
        }
        ilock(ip);
    } while (ip->type == T_SYMLINK);
    return ip;
}
